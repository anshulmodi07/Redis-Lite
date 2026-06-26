# Redis Lite — Improvement Plan

> **Context**: `server_v12_bin` on WSL2, benchmarked against Real Redis 7.2.7.  
> **Goal**: Diagnose every failed benchmark, trace each failure to its root cause in the
> event-loop / I/O layer, and provide concrete, ordered fixes with expected outcomes.
> Secondary goal: audit the guide itself for design gaps that led to these results.

---

## Part 1 — Guide Audit: Flaws and Loopholes

The guide is excellent for building correctness. But it has several gaps that set the
implementation up for the benchmark failures seen in `analysis.md`. These are not bugs in
the guide — they reflect a deliberate choice to defer I/O performance to V12. The problem
is V12 only says "run the benchmark and explain the gap" rather than prescribing how to close it.

### Flaw 1 — V11.0 Pipelining section is wrong

> *"Pipelining is already implicit in your RESP parser. Your event loop already does this
> correctly."*

This is only true for a single client. With multiple clients, the guide's `while(parser.tryParse(argv)) dispatch(argv)` loop processes **all** pipelined commands from client A before returning to the epoll loop — starving every other client's writes while A's responses pile up. The guide never addresses cross-client fairness.

**Missing instruction**: Cap the number of commands processed per client per event-loop
iteration (a "command budget"), and interleave write-draining across all clients.

---

### Flaw 2 — V2.1 EPOLLOUT management is underspecified

The guide says:

> *"Only register EPOLLOUT when you actually have data to send."*

This is correct. But it leaves out what to do when the write buffer *can't* drain completely
(i.e., the kernel's socket send buffer is full and `write()` returns EAGAIN). The natural but
wrong fix — stopping all reads from this client until EPOLLOUT fires — is exactly what the
post-IO optimization did, and it caused the **C=100 SET -67% regression**.

The guide also never explains the **inline-write optimization**: before arming EPOLLOUT at all,
try a direct `write()` immediately after command dispatch. Most of the time it succeeds (the
kernel buffer isn't full), and you avoid an entire extra event-loop round-trip per response.
This alone could cut baseline latency by 30–50%.

**Missing instruction**: Attempt an inline `write()` immediately after building the response.
Only arm EPOLLOUT if the write returns EAGAIN. If it partially succeeds, move the unconsumed
remainder into `write_buf` and then arm EPOLLOUT.

---

### Flaw 3 — No read-budget concept

The guide's V2.0 event-loop pseudocode says:

```
for each client fd that is POLLIN:
    bytes = read(fd, buf, sizeof(buf))
    parser.feed(bytes)
    while parser.tryParse(argv): result = dispatch(argv); client.write_buf += result
```

With pipelining, one client can deliver 256 commands in a single `read()`. The `while` loop
processes all 256, building 256 responses in `write_buf`, before any other client's writes
get flushed. Under C=50 with P=16, this creates write starvation for all other clients.

**Missing instruction**: Add a `max_commands_per_read = 64` constant. After processing that
many commands from one client, break out of the inner loop. The level-triggered EPOLLIN will
fire again next iteration, giving other clients a chance to drain their write buffers first.

---

### Flaw 4 — V12.2 benchmark targets are not achievable with the guide's architecture

The guide's README table says pipeline=16 should reach "800k+ ops/sec". That is Real Redis's
number. A correct single-threaded C++17 implementation without jemalloc, SDS, or kernel
send-buffer tuning will reach roughly 50–150k SET req/s at P=16, not 800k. The guide sets an
unmeasurable target, making the benchmark section feel like a failure when it is actually
correct behaviour for the architecture.

**Fix**: Replace the guide's target numbers with realistic ranges:

| Scenario | Realistic Redis Lite target | Real Redis |
|----------|----------------------------|------------|
| SET/GET, no pipeline | 15–30k / 40–80k req/s | 76k / 78k |
| SET, pipeline P=16 | 40–80k req/s | 625k |
| SET, concurrency C=50 | 30–60k req/s | 870k |

---

### Flaw 5 — No write-fairness architecture described anywhere

The guide has no mention of a write-scheduling queue, write budgets, or round-robin flushing.
The entire write path is "add to `write_buf`, flush on EPOLLOUT". For a single client this is
fine. For 50–256 concurrent clients each sending pipelined requests, it is the root cause of
the GET collapse at C≥50.

**Missing architecture**: A per-event-loop-iteration round-robin over all clients with pending
writes, each limited to N bytes (e.g., 16 KB) per turn. This is how real event-loop servers
achieve fairness without threads.

---

### Flaw 6 — `std::string write_buf` is a hidden allocator bottleneck

The guide tells you to use `std::string write_buf`. Erasing from the front of a string —
`write_buf.erase(0, n)` — is O(N) because it shifts all remaining bytes. Appending
response strings is also O(N) amortized but triggers reallocations. The baseline SET gap
(~17×) is partly caused by `std::string` churn on the hot path.

**Fix**: Replace `write_buf` with a `std::vector<char>` plus a `size_t head_` cursor, or a
proper ring buffer. This avoids the O(N) erase and removes mid-benchmark reallocations.

---

## Part 2 — Root Cause Analysis for Every Failing Benchmark

### Benchmark 1 — Baseline SET ~17× slower than Real Redis

**Symptoms**: 4,466 → 5,608 req/s after optimization. Real Redis: 76k.

**Root causes (in order of impact)**:

1. **No inline-write**: Every response goes through the EPOLLOUT round-trip. That means each
   SET = read event → dispatch → append to write_buf → next loop tick → EPOLLOUT fires →
   write(). Two event-loop iterations per command. Real Redis writes inline, so it's one
   round-trip.

2. **`std::string write_buf` with front-erase**: After every partial write, `write_buf.erase(0, n)`
   shifts the remaining bytes. At 5k req/s with ~8-byte responses, that's 40k memmoves per
   second of response data.

3. **Per-command string construction**: Every response is a new `std::string` (e.g.,
   `encodeOK()` returns `"+OK\r\n"` as a heap allocation). At 5k req/s this is 5k malloc/free
   pairs per second just for `+OK` responses.

4. **System malloc vs jemalloc**: Real Redis uses jemalloc which has per-thread arenas and
   avoids lock contention. Your system malloc on WSL2 adds ~200–400 ns per allocation.

5. **WSL2 loopback overhead**: WSL2 adds ~0.1–0.3 ms RTT vs native Linux. This alone accounts
   for 3–5k max req/s at no-pipeline (1 RTT per request = 1s / 0.2ms = 5,000 req/s ceiling).
   **This means the baseline SET bottleneck is fundamentally WSL2 RTT, not code quality.**
   A realistic target for SET no-pipeline on WSL2 is ~5–8k req/s.

**Fix priority**: (1) inline-write path, (2) ring-buffer write_buf, (3) static string literals for common responses.

---

### Benchmark 1 — Baseline GET ~3× slower than Real Redis

**Symptoms**: 25,202 → 30,423 req/s. Real Redis: 78k.

GET is faster than SET because the response is larger (~10-30 bytes for a bulk string vs 5 bytes
for `+OK`), which means the write attempt is more likely to saturate the kernel buffer and the
RTT proportion is smaller. The 3× gap is smaller because GET already benefits from warm-memory
access and the response is flushed in one `write()` call more often.

**This gap is acceptable and expected on WSL2. Target: 40–50k GET req/s after inline-write fix.**

---

### Benchmark 2 — SET Pipeline P=256: only 2.5× improvement vs 15× for Real Redis

**Symptoms**: 4,908 → 12,427 req/s (P=1→P=256). Real Redis: 82k → 1.22M.

**Root causes**:

1. **Write buffer bottleneck at depth**: At P=256, each event-loop read may parse 256 commands
   and build 256 responses in `write_buf`. The `write_buf` can grow to 256 × ~5 bytes = ~1.3 KB
   before flushing, which is fine — but the issue is `std::string::append` triggering capacity
   doublings (1KB → 2KB → 4KB → ...) during the burst. The growth is amortized but creates
   jitter at the start of each pipeline batch.

2. **EPOLLOUT not firing fast enough**: After reading 256 commands, the server appends all
   256 responses to `write_buf` and then returns to `epoll_wait`. On the next tick, it fires
   EPOLLOUT and tries to flush. But if the kernel send buffer is smaller than 256 responses,
   it needs multiple EPOLLOUT ticks to fully drain. p50 latency of 921 ms at P=256 confirms
   this: responses are queuing for multiple event-loop iterations.

3. **writev iovec cap at 64**: If `pending_writes` is split into chunks capped at 64 iovecs,
   a 256-command pipeline may require 4 writev calls per flush, each a syscall. Real Redis
   uses a contiguous output buffer and a single `write()`.

**Fix**: Use a contiguous ring-buffer for `write_buf`. Pre-allocate at 64 KB. Use a single
`write()` (not writev) on the contiguous buffer. Reserve capacity upfront on connection.

---

### Benchmark 2 — GET Pipeline erratic at mid-depth (P=4–16)

**Symptoms**: GET req/s: 31k (P=1) → 12k (P=4) → 6k (P=16) → 81k (P=256).

This U-shape is distinctive. It points to a **partial-response flush issue**:

At P=4–16, the GET responses are large enough that the combined response (e.g., 16 × ~15 bytes
bulk string = ~240 bytes) sometimes doesn't fit in a single `write()`. The remainder stays in
`write_buf` and waits for EPOLLOUT. But the RESP parser on the client side is waiting for all
P responses before sending the next batch. So the server is waiting for EPOLLOUT while the
client is waiting for the server to finish — a mini-deadlock at the socket-buffer level.

At P=256, the combined response is large enough that the client starts consuming bytes before
the server has flushed everything, breaking the deadlock stall.

**Fix**: The inline-write path (attempt `write()` immediately after building responses)
eliminates this entirely. If the inline write succeeds fully, EPOLLOUT is never armed and
the client gets its responses immediately without waiting for the next event-loop tick.

---

### Benchmark 3 — C=100 SET dropped 67% after post-IO-optimization

**Symptoms**: Before: 10,330 req/s. After EPOLLOUT optimization: 3,364 req/s. **This is a regression.**

**Root cause — overly aggressive backpressure creates a cascading stall**:

The post-IO fix arms EPOLLOUT backpressure by pausing `recv` when the send buffer is full.
With C=100 clients each sending pipelined commands at P=16:

1. Client A sends 16 commands → server reads all, builds 16 responses in `write_buf`
2. Server tries to flush A's `write_buf` inline → kernel send buffer partially accepts
3. Server pauses `recv` on A (EPOLLIN removed) and arms EPOLLOUT for A
4. Before EPOLLOUT for A fires, clients B, C, D… all arrive with reads
5. Server processes B → same thing. C → same. After ~6 clients, all have EPOLLIN paused
6. epoll_wait now only has EPOLLOUT events for 6+ clients
7. Server drains them one by one, but new EPOLLIN for the other 94 clients can't fire until
   EPOLLOUT drains are complete
8. Cascading stall: all 100 clients are blocked on write-drain before any new reads arrive

This is a **write-side thundering herd** caused by removing EPOLLIN too aggressively.

**Fix**: Never remove EPOLLIN. Instead:
- Maintain a separate `write_pending_queue` (a `std::deque<int>` of client fds with non-empty write_buf)
- In each event-loop iteration, after processing all EPOLLIN, round-robin through `write_pending_queue` draining at most `WRITE_BUDGET` bytes (e.g., 16 KB) per client
- EPOLLOUT is only used as a signal that the kernel is ready, not as the primary scheduling mechanism

---

### Benchmark 3 — GET collapse at C≥50 (persists after optimization)

**Symptoms**: C=1: 39k, C=10: 70k, C=50: 6k, C=100: 4k, C=256: 4k.

**Root cause — no cross-client write fairness**:

At C=50 with P=16, each client has 16 responses pending in its `write_buf` after one
event-loop read. The server processes clients in the order epoll_wait returns them. If
client A's write takes longer to flush (e.g., large GET values or a slow client), the
server spends many EPOLLOUT ticks on A alone, while clients B through Z accumulate more
unread incoming commands. Eventually the benchmark tool's `redis-benchmark` client-side
timeout expires (default 60s) and reports 0 ops/sec bursts.

The "0 rps burst" mentioned in the analysis is exactly a timeout burst — a client stalled
so long that it missed the measurement window, then caught up.

**Fix**: Round-robin write scheduling with per-client byte budget. See Fix 3 below.

---

## Part 3 — Fixes (Ordered by Impact)

### Fix 1 — Inline-Write Path (Highest Impact)

**Expected improvement**: Baseline SET +50–80% (from 5.6k to ~8–10k on WSL2). Eliminates
the erratic GET pipeline behaviour. Eliminates C=100 SET regression entirely.

**Location**: `eventloop.cpp` — the read-handler, immediately after the `while(tryParse)` loop.

**Concept**: After building all responses in `write_buf`, attempt a direct `write()` before
returning to `epoll_wait`. If it fully succeeds, no EPOLLOUT is needed. If it partially
succeeds, move the cursor forward. Only if it returns EAGAIN (kernel buffer full) should
EPOLLOUT be armed.

```cpp
// After: while (client.parser.tryParse(argv)) { ... client.write_buf += dispatch(argv); }

// --- INLINE WRITE ATTEMPT ---
if (!client.write_buf.empty()) {
    ssize_t n = write(client.fd,
                      client.write_buf.data() + client.write_head,
                      client.write_buf.size() - client.write_head);

    if (n > 0) {
        client.write_head += n;
        if (client.write_head == client.write_buf.size()) {
            // Fully flushed: reset buffer, no EPOLLOUT needed
            client.write_buf.clear();
            client.write_head = 0;
            disarm_epollout(client);   // remove EPOLLOUT if it was armed
        } else {
            // Partial flush: arm EPOLLOUT for the remainder
            arm_epollout(client);
        }
    } else if (n < 0 && errno == EAGAIN) {
        // Kernel buffer full: arm EPOLLOUT
        arm_epollout(client);
    } else if (n < 0) {
        // Real error: close client
        close_client(client);
    }
    // n == 0 is not possible on a socket write
}
```

The critical detail: **do not remove EPOLLIN based on write-buffer fullness**. Just arm EPOLLOUT
and keep reading. The two signals are independent. Only arm EPOLLOUT when needed; only disarm it
when the buffer drains to zero.

---

### Fix 2 — Replace `std::string write_buf` with a Ring Buffer

**Expected improvement**: Baseline SET +20–30% from reduced malloc/memmove. Eliminates P=256
throughput cliff caused by string growth jitter.

```cpp
// In client.h
struct WriteBuffer {
    std::vector<char> buf;   // pre-allocated at connection time
    size_t head = 0;         // read cursor (bytes already written to socket)
    size_t tail = 0;         // write cursor (next byte to append)

    WriteBuffer() { buf.reserve(65536); } // 64 KB upfront

    void append(const char* data, size_t len) {
        // Compact if head has consumed more than half the buffer
        if (head > buf.capacity() / 2) {
            size_t remaining = tail - head;
            std::memmove(buf.data(), buf.data() + head, remaining);
            tail = remaining;
            head = 0;
        }
        buf.insert(buf.end(), data, data + len);
        tail += len;
    }

    const char* read_ptr() const { return buf.data() + head; }
    size_t readable()      const { return tail - head; }
    void consume(size_t n)       { head += n; }
    bool empty()           const { return head == tail; }
    void reset()                 { head = tail = 0; }
};
```

Replace all `client.write_buf += encodeXxx(...)` with `client.write_buf.append(...)` calls.
The `head`/`tail` cursor avoids the O(N) erase. The 64 KB pre-allocation avoids reallocs
during the first several hundred pipelined responses.

---

### Fix 3 — Fair Cross-Client Write Scheduling (Fixes GET Collapse at C≥50)

**Expected improvement**: GET at C=50 from 6k → 30–50k req/s. SET at C=100 from 3.3k → 8–12k
(reverting the regression). GET stalls eliminated.

**Concept**: Maintain a `write_ready` deque of client fds that have non-empty write buffers.
In each event-loop iteration, after all EPOLLIN events are processed, round-robin through
`write_ready` draining at most `WRITE_BUDGET_BYTES` per client.

```cpp
// In eventloop.h
static constexpr size_t WRITE_BUDGET_BYTES  = 16384;  // 16 KB per client per tick
static constexpr int    CMD_BUDGET_PER_READ = 64;     // max commands per EPOLLIN per tick

std::deque<int> write_ready;          // fds with pending writes
std::unordered_set<int> in_write_ready; // O(1) membership check

void enqueue_write(int fd) {
    if (in_write_ready.insert(fd).second)  // only add once
        write_ready.push_back(fd);
}

// Main loop (simplified):
void event_loop() {
    while (true) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, TIMER_MS);

        // --- 1. Accept new connections ---
        if (server_fd_ready(events, n))
            do_accept();

        // --- 2. Process EPOLLIN events (read + dispatch, capped) ---
        for (int i = 0; i < n; i++) {
            if (!(events[i].events & EPOLLIN)) continue;
            int fd = events[i].data.fd;
            auto& client = clients[fd];

            char buf[65536];
            ssize_t bytes = read(fd, buf, sizeof(buf));
            if (bytes <= 0) { close_client(fd); continue; }

            client.parser.feed(buf, bytes);
            int cmd_count = 0;
            std::vector<std::string> argv;
            while (cmd_count < CMD_BUDGET_PER_READ && client.parser.tryParse(argv)) {
                client.write_buf.append_str(dispatch(argv));
                cmd_count++;
            }
            // If parser still has data, EPOLLIN will fire again (level-triggered)
            // Attempt inline write immediately:
            try_inline_write(client);   // Fix 1 from above
            if (!client.write_buf.empty())
                enqueue_write(fd);
        }

        // --- 3. Round-robin write drain ---
        int clients_to_drain = write_ready.size();
        while (clients_to_drain-- > 0 && !write_ready.empty()) {
            int fd = write_ready.front();
            write_ready.pop_front();
            in_write_ready.erase(fd);

            auto& client = clients[fd];
            if (client.write_buf.empty()) continue;

            // Drain up to WRITE_BUDGET_BYTES
            size_t to_write = std::min(client.write_buf.readable(), WRITE_BUDGET_BYTES);
            ssize_t n = write(fd, client.write_buf.read_ptr(), to_write);
            if (n > 0) {
                client.write_buf.consume(n);
            } else if (n < 0 && errno != EAGAIN) {
                close_client(fd);
                continue;
            }
            // Re-enqueue if still has data
            if (!client.write_buf.empty())
                enqueue_write(fd);
        }
    }
}
```

Key points:
- EPOLLOUT is not needed at all in this design for most cases. The round-robin drain loop
  replaces it. EPOLLOUT can be kept as a wakeup signal for when the kernel was full, but
  is no longer the primary scheduling mechanism.
- `CMD_BUDGET_PER_READ = 64` prevents one client with P=256 from monopolizing all the CPU.
  After 64 commands, the inner `while` exits and the next event-loop tick resumes that
  client (EPOLLIN is still armed).
- `WRITE_BUDGET_BYTES = 16 KB` ensures each client gets a fair write slice per tick. At
  C=50 this means all 50 clients get a write opportunity within one event-loop iteration.

---

### Fix 4 — Static Response Strings for Common Replies

**Expected improvement**: Baseline +5–10% from eliminating per-command heap allocations
for the most common responses.

```cpp
// In resp.h
namespace Resp {
    static const std::string OK         = "+OK\r\n";
    static const std::string PONG       = "+PONG\r\n";
    static const std::string NULL_BULK  = "$-1\r\n";
    static const std::string INT_0      = ":0\r\n";
    static const std::string INT_1      = ":1\r\n";
    static const std::string QUEUED     = "+QUEUED\r\n";
    // Pre-build integers 0–9999 at startup:
    extern std::string INTS[10000];     // populated in init()
}
```

In `dispatch()`, return `Resp::OK` (a `const std::string&`) instead of building a new string.
This avoids a malloc/free pair for every SET response.

```cpp
// Before (allocates on every call):
std::string encodeOK() { return "+OK\r\n"; }

// After (zero allocation):
const std::string& encodeOK() { return Resp::OK; }
```

The write_buf's `append_str(const std::string& s)` copies the bytes into the ring-buffer,
which is one `memcpy` to pre-allocated memory — no heap allocation.

---

### Fix 5 — Read Buffer: Single Large `recv` per EPOLLIN

**Expected improvement**: Baseline +10–15% from fewer `read()` syscalls.

The guide's V2.0 pseudocode uses `recv(fd, buf, sizeof(buf))` with a fixed small buffer
(often 1024 or 4096 bytes). At P=16, a client's 16 pipelined commands are typically
~100–200 bytes total. A 4 KB read buffer covers them in one syscall. But at P=256 with long
values, you may need multiple reads. Use a 64 KB read buffer on the stack:

```cpp
// In the EPOLLIN handler:
char read_buf[65536];
while (true) {
    ssize_t n = recv(fd, read_buf, sizeof(read_buf), 0);
    if (n > 0) {
        client.parser.feed(read_buf, n);
    } else if (n == 0) {
        close_client(fd);  // clean disconnect
        break;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // fully drained
        close_client(fd);  // error
        break;
    }
}
```

For level-triggered epoll this single-`recv` loop is fine — epoll will re-fire if more data
arrives. For edge-triggered, you must loop until EAGAIN. Using level-triggered (as the guide
recommends) means you can do a single `recv` per EPOLLIN event and still be correct.

---

### Fix 6 — Connection Pre-warming and `TCP_NODELAY`

**Expected improvement**: Baseline latency -0.1–0.2 ms per response on WSL2 (eliminates Nagle
algorithm batching delay for small responses).

```cpp
// When accepting a new client connection:
int flag = 1;
setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
```

`TCP_NODELAY` disables the Nagle algorithm, which coalesces small writes into larger TCP
segments. Without it, a 5-byte `+OK\r\n` response may be held for up to 40 ms waiting for
more data to batch with. For request-response patterns, Nagle causes visible latency spikes.
Real Redis sets `TCP_NODELAY` on every accepted connection. The guide never mentions it.

Also set on the listen socket:
```cpp
setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
```

---

### Fix 7 — Increase SOMAXCONN and `SO_RCVBUF` / `SO_SNDBUF`

**Expected improvement**: C=256 connections succeed without queuing drops.

```cpp
// After bind(), before listen():
listen(server_fd, 4096);   // SOMAXCONN default is often 128; 4096 prevents connection drops

// For each accepted client (optional, OS usually auto-tunes):
int buf = 131072;  // 128 KB
setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
```

With C=256 benchmark clients all connecting at once, a `listen()` backlog of 128 (or even the
default) can cause some connections to be dropped during setup, inflating the "0 rps burst"
problem seen in the analysis.

---

## Part 4 — Implementation Order and Expected Scores

Apply fixes in this order. Each can be measured independently with `run_fair_benchmark.sh`.

### Step 1 — Apply Fix 6 first (TCP_NODELAY) — 1 line, zero risk

```
Expected: SET baseline 5.6k → 7–9k, GET 30k → 40k
Benchmark: redis-benchmark -p 8080 -t set,get -n 100000 -q
```

### Step 2 — Apply Fix 1 (Inline-write path)

This requires changes to the EPOLLIN handler and the EPOLLOUT handler. Estimated ~30 lines.

```
Expected: SET baseline 5.6k → 8–12k
          GET baseline 30k → 45–55k
          Pipeline GET erratic behaviour eliminated
          C=100 SET regression reversed
Benchmark: All three redis-benchmark suites in run_fair_benchmark.sh
```

### Step 3 — Apply Fix 2 (Ring-buffer write_buf)

Requires changes to `client.h` only (the `write_buf` type) and updates to all sites that
append to it. Estimated ~50 lines of changes.

```
Expected: SET baseline 8k → 10–12k
          Pipeline P=256 SET from 11k → 15–20k (reduced reallocation jitter)
Benchmark: Pipeline depth curve (Benchmark 2 in the sheet)
```

### Step 4 — Apply Fix 3 (Fair write scheduling) — most complex, highest payoff

Requires restructuring the event loop in `eventloop.cpp`. Estimated ~100 lines of new code.

```
Expected: GET C=50 from 6k → 30–50k  ← main target
          GET C=100 from 4k → 20–35k
          SET C=100 fully restored and improved
Benchmark: Concurrency curve (Benchmark 3 in the sheet)
```

### Step 5 — Apply Fix 4 (Static response strings)

Requires additions to `resp.h` and `resp.cpp`. Estimated ~40 lines.

```
Expected: +5–10% across all benchmarks uniformly
```

### Step 6 — Apply Fix 5 (64 KB recv buffer) and Fix 7 (SOMAXCONN)

One-line changes each.

```
Expected: C=256 stability improvement, -5% variance in results
```

---

## Part 5 — Projected Scores After All Fixes

| Scenario | Current | After Fixes | Real Redis | Gap After |
|----------|---------|-------------|------------|-----------|
| SET, no pipeline | 5,608 | ~10–14k | 76,161 | ~5–7× |
| GET, no pipeline | 30,423 | ~50–65k | 77,821 | ~1.2–1.5× |
| SET, P=16 | 10,696 | ~20–35k | 625,000 | ~18–31× |
| SET, P=256 | 11,347 | ~20–35k | 1,220,683 | ~35–60× |
| GET, P=256 | 30,545 | ~80–120k | 1,787,429 | ~15–22× |
| SET, C=10, P=16 | 9,489 | ~20–35k | 675,676 | ~20× |
| GET, C=10, P=16 | 70,323 | ~100–150k | 806,452 | ~5–8× |
| SET, C=50, P=16 | 10,646 | ~20–40k | 869,565 | ~22–43× |
| **GET, C=50, P=16** | **6,146** | **~30–55k** | 1,010,101 | **~18–33×** |
| **SET, C=100, P=16** | **3,364** | **~15–25k** | 826,446 | **~33–55×** |
| GET, C=100, P=16 | 4,361 | ~20–40k | 943,396 | ~24–47× |

The WSL2 loopback ceiling (~0.15 ms base RTT) physically limits no-pipeline throughput to
~6–7k req/s for SET and ~30–50k req/s for GET regardless of code quality. These projected
scores are what's achievable without leaving WSL2.

---

## Part 6 — Remaining Structural Gaps (Explained, Not Fixed)

The following are expected gaps that are inherent to a learning implementation and do not
represent fixable bugs. They should be documented in the README under "Known architectural limits."

### Gap A — No jemalloc

Real Redis ships with jemalloc linked statically. jemalloc has per-thread arenas, size-class
bins, and avoids the global mutex in glibc malloc. On a write-heavy workload like SET,
malloc/free overhead can account for 10–20% of CPU time. Linking with jemalloc:

```bash
g++ -o server *.cpp -pthread -ljemalloc
```

If jemalloc is available on your system, this is a one-line change that may yield 10–20%
improvement across all SET-heavy benchmarks.

### Gap B — No SDS (Simple Dynamic Strings)

Redis's SDS stores the string length prefix *before* the char pointer, allowing O(1) `strlen`
and avoiding null-scanning. Your `std::string` is already O(1) for length, so this gap is
smaller than it sounds. The real SDS advantage is **embedded short strings**: strings ≤ 44 bytes
are stored inline in the robj without a separate heap allocation. At typical Redis key/value
sizes, this halves the allocation count for string operations.

### Gap C — No kernel send-buffer tuning

Real Redis benchmarks are run with `net.core.wmem_max` and `net.ipv4.tcp_wmem` tuned to
4–8 MB. On WSL2, kernel sysctl changes often require admin rights and may not persist.
Without large send buffers, the server hits backpressure earlier under deep pipelining.

### Gap D — Single-threaded vs multi-threaded I/O

Real Redis 7.x uses multi-threaded I/O: multiple threads each own a subset of clients and
handle reading + writing, while command dispatch still runs on the main thread. This allows
I/O parsing to scale with CPU cores. Implementing this is a significant architecture change
(requires lock-free queues between I/O threads and the command thread) and is out of scope
for V12.

---

## Part 7 — Testing Strategy After Each Fix

After every fix, run the full suite and compare against the current baseline in `benchmark_results.txt`:

```bash
# From the project root
wsl bash tests/run_fair_benchmark.sh 2>&1 | tee benchmark_results_new.txt

# Quick comparison for the critical scenarios:
diff <(grep -E "SET|GET" benchmark_results.txt) \
     <(grep -E "SET|GET" benchmark_results_new.txt)
```

The three scenarios to watch after each fix:

1. **Fix 1 (inline-write)**: Watch Benchmark 1 (no-pipeline) and Benchmark 3 (C=100 SET). The C=100 regression must be gone.
2. **Fix 3 (fair scheduling)**: Watch Benchmark 3 (C≥50 GET). GET at C=50 must exceed 20k req/s (up from 6k).
3. **All fixes together**: Run the full P-sweep (P=1,4,16,64,256) for GET. The U-shape must become monotonically increasing or flat.

### Red flags to catch during testing

- **Any scenario gets worse than the "Before" column** from the original benchmark table: a fix has introduced a new regression. Roll back that fix and re-examine.
- **p50 latency grows faster than throughput**: write budget is too generous; reduce `WRITE_BUDGET_BYTES`.
- **C=256 results are unstable (±50% variance)**: SOMAXCONN or accept backlog is too small; apply Fix 7.

---

## Summary

| Priority | Fix | Lines Changed | Expected Gain |
|----------|-----|---------------|---------------|
| 1 | TCP_NODELAY on accept | 2 | Baseline latency -0.1ms |
| 2 | Inline-write path | ~30 | Baseline SET +50%, C=100 regression gone |
| 3 | Ring-buffer write_buf | ~50 | P=256 jitter eliminated, +15% across board |
| 4 | Fair write scheduling | ~100 | GET C=50 from 6k → 30-50k, main target |
| 5 | Static response strings | ~40 | +5–10% uniform |
| 6 | 64 KB recv buffer + SOMAXCONN | 3 | C=256 stability |

The guide's architecture is sound. The event-loop skeleton, RESP parser, and epoll integration
are all correct. The failures are concentrated in one narrow layer: **the write-side of the
event loop**. No inline-write path, no write fairness, and an overly aggressive backpressure
mechanism are the three root causes behind every failing cell in the analysis table.
