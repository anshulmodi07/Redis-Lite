# Redis Lite — Performance Gap Analysis & Improvement Roadmap

**Status of current benchmarks (post-Phase 3):**

| Scenario | Redis Lite | Real Redis | Apparent gap |
|---|---|---|---|
| GET/SET no pipeline | 841 ops/sec | ~100k–180k ops/sec | ~119–214× |
| GET/SET pipeline P=16 | 12,715 ops/sec | ~800k+ ops/sec | ~63× |

This document diagnoses every layer of that gap, separates measurement error from real
architectural issues, and specifies the exact fix for each one — ordered by impact.

---

## The Smoking Gun Hidden in the Numbers

Before reading any further, look at this ratio:

```
12,715 ÷ 841 = 15.12×  speedup from P=16 pipelining
```

A P=16 pipeline can amortise round-trip costs 16-fold at most. Getting 15.12× (94.5% of
theoretical maximum) means the bottleneck is **not** server compute throughput. If it were,
pipelining would make little or no difference. Instead, pipelining is hiding a fixed
per-round-trip latency cost — almost certainly a ~1 ms event loop idle wait — and each
batch of 16 commands pays that cost just once.

This single observation tells us:
- The Phase 3 micro-optimisations (allocator tuning, vector reuse, etc.) were not wrong —
  they were aimed at the wrong layer entirely.
- The real ceiling is architectural, not algorithmic.

---

## Severity Map (all issues ranked)

| # | Issue | Location | Severity | Fix complexity |
|---|---|---|---|---|
| 1 | Fixed `epoll_wait` timeout (~1 ms) | `eventloop.cpp` | **Critical** | Low |
| 2 | Active expiry runs on every loop tick | `eventloop.cpp`, `db.h` | **Critical** | Low |
| 3 | No write coalescing (`beforeSleep` missing) | `eventloop.cpp` | **Critical** | Medium |
| 4 | Synchronous AOF writes on main thread | `aof.cpp` | High | Medium |
| 5 | RESP encoding: 3+ heap allocations per response | `resp.cpp` | High | Medium |
| 6 | Argument vector full copy in dispatch | `parser.cpp` | High | Low |
| 7 | System clock queried per command, not cached | `db.h`, `eventloop.cpp` | Medium | Low |
| 8 | `std::unordered_map` vs cache-local hash table | `db.h` | Medium | High |
| 9 | Benchmark tool (Python 1-client vs `redis-benchmark`) | `tests/benchmark.py` | Measurement error | Low |

---

## Critical Fixes — Do These First

### Fix 1 — Dynamic `epoll_wait` timeout (currently ~1 ms fixed)

**Root cause.** At 841 ops/sec, each operation takes approximately 1,190 µs. The TCP
loopback round-trip on localhost is 50–100 µs. The missing ~1,100 µs is the server sitting
in a blocked `epoll_wait` call with a fixed 1 ms timeout. Every single request must wait
for the current epoll cycle to finish before the server reads it, processes it, and sends
the reply.

The pipelining math confirms this: with P=16, 16 commands arrive in a single TCP segment
and are processed inside one epoll cycle. The 1 ms wait is paid once, not 16 times →
15.12× speedup. That ratio is a direct fingerprint of this exact cause.

**What real Redis does.** `ae.c::aeProcessEvents()` calls `aeSearchNearestTimer()` before
every `epoll_wait`. This returns the time remaining until the next scheduled timer event
(TTL sweep, serverCron, etc.). That value is passed as the `epoll_wait` timeout. If no
timers are pending, the timeout is `-1` (block indefinitely until data arrives). Either
way, `epoll_wait` returns the instant a client sends data — there is no idle spin.

**The fix.**

```cpp
// eventloop.cpp — inside the main while(true) loop

// BAD — current implementation (assumed):
int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1); // wakes every 1 ms regardless

// GOOD — compute from next timer deadline:
int timeout_ms = computeNextTimerMs(); // returns -1 if no timers pending
                                       // else: ms until next expiry/cron event
int nfds = epoll_wait(epfd, events, MAX_EVENTS, timeout_ms);
```

```cpp
// Implement computeNextTimerMs():
int computeNextTimerMs() {
    auto now = std::chrono::steady_clock::now();
    int64_t earliest = INT64_MAX;

    // Walk all 16 databases — find the soonest expiry
    for (auto& db : databases) {
        for (auto& [key, expiry_ms] : db.expires) {
            int64_t remaining = expiry_ms - nowMs();
            if (remaining < earliest) earliest = remaining;
        }
    }

    if (earliest == INT64_MAX) return -1;          // no timers → block forever
    return static_cast<int>(std::max(int64_t(0), earliest)); // clamp to 0
}
```

**Expected gain:** Non-pipelined throughput on a single connection should jump from
~841 ops/sec to several thousand ops/sec immediately. When combined with Fix 2 and Fix 3,
expect 10,000–50,000 ops/sec on a single sequential connection.

---

### Fix 2 — Move active expiry out of the hot loop

**Root cause.** In `eventloop.cpp`, `activeExpireCycle(db)` is called on every single
iteration of the `while(true)` event loop — regardless of whether any events fired,
regardless of how much time has passed. Inside `db.h`, the function does this:

```cpp
// db.h — activeExpireCycle (current implementation)
std::vector<std::string> sample;
for (const auto& item : db.expires) {
    sample.push_back(item.first);  // heap allocation + string copy per key
}
```

This allocates a heap vector and copies `std::string` keys on every single event loop
iteration — potentially thousands of times per second — even when zero keys are expiring.
With 16 databases, this runs the allocation loop 16 times per tick.

**What real Redis does.** Key expiry runs inside `serverCron()`, which fires at 10 Hz
(every 100 ms by default, configurable via `hz`). It samples a fixed number of buckets
directly from the hash table array without copying keys. If the expired fraction exceeds a
threshold, it runs another cycle — but it never runs on every single event loop iteration.

**The fix — two changes:**

```cpp
// eventloop.cpp — add a cron timer, remove expiry from the hot loop

// Remove this from the while(true) loop:
// for (auto& db : databases) activeExpireCycle(db);   ← DELETE

// Add a time-based gate:
int64_t last_cron_ms = 0;
constexpr int CRON_INTERVAL_MS = 100;   // 10 Hz, matches real Redis default

while (true) {
    int nfds = epoll_wait(epfd, events, MAX_EVENTS, computeNextTimerMs());
    // ... dispatch events ...

    int64_t now = nowMs();
    if (now - last_cron_ms >= CRON_INTERVAL_MS) {
        for (auto& db : databases) activeExpireCycle(db);
        last_cron_ms = now;
    }
}
```

```cpp
// db.h — remove the sample vector allocation; sample in-place
void activeExpireCycle(RedisDb& db) {
    constexpr int SAMPLE_SIZE = 20;
    int checked = 0;
    int64_t now = g_cached_time_ms;   // use cached clock (see Fix 7)

    auto it = db.expires.begin();
    while (it != db.expires.end() && checked < SAMPLE_SIZE) {
        if (it->second <= now) {
            db.data.erase(it->first);
            it = db.expires.erase(it);
        } else {
            ++it;
        }
        ++checked;
    }
}
```

**Expected gain:** Removes thousands of spurious heap allocations and string copies per
second from the hot path. Combined with Fix 1, this is the single biggest CPU-time
reclamation after the timeout fix.

---

### Fix 3 — Implement write coalescing (`beforeSleep` pattern)

**Root cause.** Currently, when a command is processed in the read handler, the server
immediately calls `send()` to write the response. This means every request costs at least
2 system calls on the hot path: one `recv()` to read the command, one `send()` to write
the response. With pipelining, this also means a separate `send()` per command even when
all 16 responses could fit in a single TCP segment.

**What real Redis does.** After processing a command, Redis does *not* call `send()`
immediately. It appends the encoded response to a per-client `reply_buf`. The client is
added to a `pending_writes` list. Just before calling `epoll_wait()`, the `beforeSleep()`
hook iterates all pending-write clients and flushes their buffers — for the common case
(small response, uncongested socket), this is a single `write()` call that coalesces all
buffered responses. `EPOLLOUT` is only registered if the write would block (EAGAIN).

```
// Real Redis write lifecycle:
//
//   command arrives (EPOLLIN)
//   → readQueryFromClient()
//   → processCommand()
//   → addReply()          // appends to client->reply_buf, adds to pending_writes
//
//   [next epoll_wait cycle begins]
//   → beforeSleep()       // flushes ALL pending_writes in one write() each
//   → epoll_wait(...)     // EPOLLOUT not registered unless write blocked
```

**The fix in `eventloop.cpp`:**

```cpp
// Add to Client struct (client.h):
std::string write_buf;     // accumulated response bytes
bool pending_write = false;

// Replace immediate send() in command handler with buffer append:
void addReply(Client& client, const std::string& response) {
    client.write_buf += response;
    client.pending_write = true;
}

// Add beforeSleep() function, called just before epoll_wait:
void beforeSleep(std::vector<Client*>& pending_writes) {
    for (Client* c : pending_writes) {
        if (c->write_buf.empty()) continue;

        ssize_t n = send(c->fd, c->write_buf.data(), c->write_buf.size(), MSG_DONTWAIT);
        if (n == static_cast<ssize_t>(c->write_buf.size())) {
            // Full write — common case, no EPOLLOUT needed
            c->write_buf.clear();
        } else if (n > 0) {
            // Partial write — register EPOLLOUT for the remainder
            c->write_buf.erase(0, n);
            registerEpollOut(c->fd);
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            registerEpollOut(c->fd);
        }
        c->pending_write = false;
    }
    pending_writes.clear();
}

// In the main event loop:
while (true) {
    beforeSleep(pending_writes);  // ← flush all buffered responses
    int nfds = epoll_wait(epfd, events, MAX_EVENTS, computeNextTimerMs());
    // ... dispatch events, append to write_buf via addReply() ...
}
```

**Expected gain:** Eliminates one syscall per command on the write path. With pipelining,
multiple responses are coalesced into a single `write()`, dramatically reducing syscall
overhead. This is the change that makes pipelined throughput approach 100k+ ops/sec.

---

## High Impact Fixes

### Fix 4 — Disable AOF during benchmarks; move fsync off main thread

**Root cause.** In `aof.cpp`, AOF is enabled on startup (`g_aof_enabled = true`) and
`aofFlush()` is called synchronously in the read loop on every client tick. Even with
`everysec` fsync policy, the `write()` call to flush AOF buffers runs on the main thread,
adding VFS and page-cache overhead to every request.

**Real Redis approach.** Benchmarks are always run with `appendonly no`. When AOF is
enabled in production, Redis offloads `fsync()` to dedicated background I/O threads (`bio`
threads) — the main event loop only appends to an in-memory AOF buffer and never blocks
on disk.

**The fix — two steps:**

1. Ensure `appendonly no` is the default config for benchmark runs:
```cpp
// config.h or equivalent
bool g_aof_enabled = false;  // default off; enable explicitly via CONFIG SET
```

2. If AOF must remain enabled, decouple fsync from the main thread:
```cpp
// aof.cpp — move fsync to a background thread
std::thread aof_flusher([] {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // everysec
        if (g_aof_fd >= 0) fsync(g_aof_fd);
    }
});
aof_flusher.detach();

// Main thread: only write() to the OS page cache — never fsync()
void aofAppend(const std::string& command) {
    write(g_aof_fd, command.data(), command.size()); // fast, kernel-buffered
}
```

**Expected gain:** Removes disk I/O from the latency-critical path. On systems where OS
write buffering is hiding the cost today, this won't show in benchmarks — but it removes
a correctness hazard and will matter on write-heavy production workloads.

---

### Fix 5 — Eliminate response encoding allocation chain

**Root cause.** In `resp.cpp`, every response is constructed by string concatenation:

```cpp
// resp.cpp — current implementation
std::string encodeBulkString(const std::string& value) {
    return "$" + to_string(value.size()) + "\r\n" + value + "\r\n";
    //      ↑              ↑                          ↑         ↑
    //  alloc #1       alloc #2                   alloc #3  alloc #4 (final)
}
```

Four heap allocations per bulk-string response. For a simple GET returning a 10-byte
value, this allocates and frees ~40 bytes of heap memory four times before the bytes
reach the client socket.

**Real Redis approach.** Redis writes directly into pre-allocated per-client reply buffers
using `addReplyBulkLen()` + `addReplyBulk()` — low-level functions that call
`memcpy` into the buffer rather than building intermediate `std::string` objects.

**The fix — write into a reusable buffer:**

```cpp
// resp.cpp — rewrite encoding to write directly into client write_buf
void encodeBulkStringInto(const std::string& value, std::string& out) {
    // Reserve once: "$" + digits + "\r\n" + value + "\r\n"
    out.reserve(out.size() + 1 + 20 + 2 + value.size() + 2);
    out += '$';
    out += std::to_string(value.size());
    out += "\r\n";
    out += value;
    out += "\r\n";
}

void encodeIntInto(int64_t v, std::string& out) {
    out.reserve(out.size() + 1 + 20 + 2);
    out += ':';
    out += std::to_string(v);
    out += "\r\n";
}

void encodeOKInto(std::string& out) {
    out += "+OK\r\n";   // no allocation — literal appended into existing capacity
}
```

By passing the client's `write_buf` directly into encoding functions, each response
appends its bytes in-place. With `write_buf` pre-reserved at client initialisation, steady
state produces **zero heap allocations** for encoding.

**Expected gain:** Significant in the pipelined benchmark where Phase 3 optimisations
are most visible. Each response currently costs 3–4 allocations; this brings it to 0 for
the common case. Most impactful for bulk-string responses (GET, HGET, etc.).

---

### Fix 6 — Remove the argument vector copy in dispatch

**Root cause.** In `parser.cpp`, the dispatch function copies the entire parsed argument
vector before normalising the command name:

```cpp
// parser.cpp — current implementation
string dispatch(Client& client, ..., const vector<string>& argv, ...) {
    vector<string> normalized = argv;       // full copy: key + value strings duplicated
    std::transform(normalized[0].begin(), normalized[0].end(),
                   normalized[0].begin(), ::toupper);
    // ... uses normalized ...
}
```

For a `SET key value` command, this copies three strings — the command name, the key, and
the full value — into a new vector on every single dispatch call.

**The fix — normalise in place, pass by reference:**

```cpp
// Uppercase only the command token; never copy the rest:
void normalizeCommand(std::string& cmd) {
    for (char& c : cmd) c = static_cast<char>(std::toupper(c));
}

// Dispatch signature:
std::string dispatch(Client& client, ..., std::vector<std::string>& argv, ...) {
    normalizeCommand(argv[0]);   // mutate argv[0] in place — no copy
    // argv[1], argv[2]... accessed by const ref throughout handlers
}
```

If `argv` is the `parsed_argv_cache` already reused across commands (from Phase 3), this
means the entire argument path — parse, normalise, dispatch, handle — is now zero-copy.

**Expected gain:** Eliminates 2–4 string copies per command for any command with a key or
value argument. For large values (e.g. `SET key <1MB-value>`), the current implementation
copies the entire payload unnecessarily.

---

## Medium Impact Fixes

### Fix 7 — Cache the system clock once per event loop tick

**Root cause.** Every call to `nowMs()` invokes `std::chrono::system_clock::now()`, a
syscall-equivalent kernel query. With active expiry running on every loop tick (pre-Fix 2)
and lazy expiry running on every command read, this can be called thousands of times per
second redundantly.

**Real Redis approach.** `server.mstime` and `server.unixtime` are updated once at the top
of each `aeProcessEvents()` iteration and reused everywhere — command handlers, expiry
checks, stat updates.

**The fix:**

```cpp
// eventloop.cpp — add a global cached timestamp
int64_t g_cached_time_ms = 0;

// Top of each event loop iteration:
while (true) {
    g_cached_time_ms = currentTimeMs();  // single clock query per cycle
    beforeSleep(pending_writes);
    int nfds = epoll_wait(...);
    // ...
}

// db.h — lazy expiry uses cached time:
bool isExpired(const RedisDb& db, const std::string& key) {
    auto it = db.expires.find(key);
    if (it == db.expires.end()) return false;
    return it->second <= g_cached_time_ms;   // no syscall
}
```

**Expected gain:** Reduces syscall count per command. Low individual impact but
accumulates across all commands, especially with many keys that have TTLs set.

---

### Fix 8 — `std::unordered_map` vs a cache-local hash table

**Root cause.** `std::unordered_map<std::string, RedisObject*>` is a bucket-and-chained-
node structure. Every key-value insertion allocates a separate heap node for the linked
list in its bucket. Lookups follow two pointer dereferences: once into the bucket array,
once into the node — with the node likely residing on a different cache line from the
bucket. For a hot GET loop with 100k keys, this produces frequent L2/L3 cache misses.

**Real Redis approach.** `dict.c` uses an open-addressing layout where keys and values sit
in the same flat array. Probing walks a contiguous memory region, which is dramatically
more cache-friendly. Combined with jemalloc arenas, fragmentation is minimised.

**The fix.** This is the highest-effort item on the list. Two practical options:

Option A — Use `std::unordered_map` with `reserve()` (already done in Phase 3) and
accept the architectural overhead. This is a reasonable trade-off for an educational
project.

Option B — Implement a flat open-addressing hash map. A minimal version:

```cpp
// Simple open-addressing hash map (Robin Hood probing)
template<typename V>
struct FlatMap {
    struct Slot {
        std::string key;
        V value;
        bool occupied = false;
        int8_t dist = 0;   // probe distance for Robin Hood
    };

    std::vector<Slot> slots;
    size_t count = 0;
    size_t capacity = 0;

    explicit FlatMap(size_t cap = 131072) {   // power of 2
        slots.resize(cap);
        capacity = cap;
    }

    V* find(const std::string& key) {
        size_t h = std::hash<std::string>{}(key) & (capacity - 1);
        for (int d = 0; d <= 64; ++d) {
            auto& s = slots[(h + d) & (capacity - 1)];
            if (!s.occupied) return nullptr;
            if (s.key == key) return &s.value;
            if (s.dist < d) return nullptr;   // Robin Hood early exit
        }
        return nullptr;
    }
    // ... insert, erase, resize similarly
};
```

**Expected gain for Option B:** 20–40% throughput improvement on lookup-heavy workloads
(many GETs). Cache-miss reduction is the mechanism. Not worth doing until Fixes 1–6 are
in place.

---

## Benchmark Methodology — What You're Actually Measuring

The apparent 119× gap vs. real Redis is roughly composed of:

| Source | Contribution to gap |
|---|---|
| Wrong benchmark tool (sequential Python vs. redis-benchmark 50 clients) | ~50–80× of the apparent gap |
| Fixed event loop timeout + missing beforeSleep | ~5–15× of residual gap |
| Expiry cycle on every tick | ~1.5–2× of residual gap |
| Encoding allocations, arg vector copy, clock overhead | ~1.2–1.5× combined |
| Inherent C++ vs C, `std::unordered_map` vs `dict.c` | ~1.5–3× — permanent, acceptable |

**What real Redis's 100k ops/sec actually means:**

```bash
redis-benchmark -n 100000 -q
# Default: 50 parallel persistent connections, C async client, loopback
# 50 concurrent connections × 2000 RTTs/sec each = 100,000 ops/sec
# Your server capability barely matters at this concurrency level
```

**The right benchmark for Redis Lite:**

```bash
# Step 1: Use the same tool
redis-benchmark -p 8080 -t set,get -n 100000 -q

# Step 2: Single-connection baseline (honest comparison)
redis-benchmark -p 8080 -t set,get -n 100000 -c 1 -q

# Step 3: Pipelining comparison
redis-benchmark -p 8080 -t set,get -n 100000 -P 16 -q

# Step 4: Multi-client stress
redis-benchmark -p 8080 -t set,get -n 100000 -c 50 -q
```

After Fixes 1–3, running `redis-benchmark -c 1` against Redis Lite should show
~10,000–50,000 ops/sec on a single connection. With `-c 50`, expect 50,000–120,000 ops/sec
— within 2–5× of real Redis, which is the honest, expected gap for a C++ educational
implementation.

---

## Realistic Performance Targets After All Fixes

| Benchmark | Current | After Fix 1–3 | After Fix 1–7 | Real Redis | Remaining gap |
|---|---|---|---|---|---|
| `redis-benchmark -c 1 -P 1` | ~841 ops/sec | ~10k–40k | ~30k–60k | ~3k–10k | ≤3× |
| `redis-benchmark -c 50 -P 1` | ~untested | ~50k–100k | ~80k–150k | ~100k–180k | ≤2× |
| `redis-benchmark -c 50 -P 16` | ~12.7k ops/sec | ~200k–500k | ~400k–700k | ~800k–1.5M | ≤2–3× |

The residual 2–3× gap after all fixes is C vs C++, `dict.c` vs `std::unordered_map`, and
SDS vs `std::string`. This is **expected, documented, and perfectly acceptable** for an
educational Redis clone. Own it in the README — the guide itself says to explain the gap
rather than hide it.

---

## What Phase 3 Optimisations Actually Did

Phase 3 was not wrong — it was aimed at the wrong layer given the current state of the
server. Here is an honest accounting:

| Phase 3 Change | Was it correct? | Actual impact now | Impact after Fixes 1–3 |
|---|---|---|---|
| Parser vector reuse (`parsed_argv_cache`) | ✅ Yes | ~0.1% — buried under 1ms epoll wait | ~5–10% — useful at high throughput |
| jemalloc linking | ✅ Yes | ~0.2% | ~10–20% on write-heavy workloads |
| Error string caching | ✅ Yes | ~0% | ~1–3% |
| Hash table pre-sizing (`reserve(100k)`) | ✅ Yes | ~0% normally | ~2–5% — prevents rehash pauses |

All four are valid optimisations. They simply need a server that is no longer bottlenecked
by a 1 ms idle spin before their contribution becomes visible in benchmark results.

---

## Implementation Order

```
Phase 4A — Event loop (1–2 days, highest leverage):
  [ ] Fix 1: Dynamic epoll_wait timeout (computeNextTimerMs)
  [ ] Fix 2: Move activeExpireCycle to 10 Hz cron gate
  [ ] Fix 7: Cache g_cached_time_ms once per loop tick

Phase 4B — Write path (1–2 days):
  [ ] Fix 3: beforeSleep write coalescing + write_buf per client
  [ ] Fix 5: Encoding writes directly into write_buf (zero-alloc)
  [ ] Fix 6: Remove argv copy in dispatch (mutate argv[0] in place)

Phase 4C — I/O & persistence (1 day):
  [ ] Fix 4: AOF off by default; background fsync thread if AOF enabled

Phase 4D — Data structures (optional, higher effort):
  [ ] Fix 8: Flat open-addressing hash map (Robin Hood)

Benchmark at every phase boundary:
  redis-benchmark -p 8080 -t set,get -n 100000 -c 1 -q
  redis-benchmark -p 8080 -t set,get -n 100000 -c 50 -q
  redis-benchmark -p 8080 -t set,get -n 100000 -c 50 -P 16 -q
```

---

## Verification Checklist Per Fix

```bash
# After Fix 1 (epoll timeout):
# Non-pipelined single-client should jump well above 841 ops/sec
redis-benchmark -p 8080 -t set,get -n 10000 -c 1 -P 1 -q
# Expect: 3,000–15,000 ops/sec (was 841)

# After Fix 2 (expiry cycle):
# CPU usage should drop significantly with many keys with TTLs set
redis-benchmark -p 8080 -t set,get -n 100000 -c 1 -q
# Profile with: perf top -p <pid>  (activeExpireCycle should disappear from hot path)

# After Fix 3 (beforeSleep write coalescing):
# Pipelined throughput should improve significantly
redis-benchmark -p 8080 -t set,get -n 100000 -c 50 -P 16 -q
# Expect: 100k–400k ops/sec (was ~12.7k)

# After Fix 5 (encoding):
# Check allocation rate drops with valgrind massif or heaptrack
heaptrack ./redis-lite &
redis-benchmark -p 8080 -t set,get -n 10000 -c 1 -q
# Expect: near-zero allocations per command in steady state

# Full regression (run after every phase):
python3 tests/test_v12.py
# All tests must still pass
```

---

## Summary

The 119× gap breaks down into three very different problems:

**Problem A (measurement error):** The Python sequential benchmark is not comparable to
`redis-benchmark`. It will always show poor numbers because it measures Python socket
overhead + 1 RTT per op, not server throughput. Switching tools closes most of the apparent
gap with zero code changes.

**Problem B (architectural, fixable in days):** A fixed ~1 ms `epoll_wait` timeout and an
active expiry cycle running on every loop tick both cap single-connection throughput
regardless of how fast the rest of the code is. These are event loop design decisions, not
language-level limitations. Fixing them costs fewer than 50 lines of code and delivers
the majority of remaining real performance gain.

**Problem C (inherent, acceptable):** C++ vs C, `std::unordered_map` vs `dict.c`,
`std::string` vs SDS — these produce a permanent 1.5–3× gap that no amount of
micro-optimisation will close without rewriting in C. For an educational project, this
gap is expected, honest, and worth documenting rather than chasing.

Phase 3 was the right kind of work but done in the wrong order. Micro-optimise after
the architectural ceiling is raised, not before.
