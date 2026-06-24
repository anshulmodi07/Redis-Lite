# Build Your Own Redis — The Complete Incremental Guide

> **Starting point**: Your friend's V0 — a thread-per-client TCP server with `unordered_map<string,string>`,
> a `mutex`, and space-delimited `GET`/`SET` over a raw text protocol on port 8080.
>
> **End goal**: A working Redis-compatible server with the real wire protocol, all five core data types,
> TTL/expiry, persistence (RDB + AOF), Pub/Sub, transactions, and optional advanced features.
>
> **Philosophy**: Each version adds **one focused idea**. You should be able to explain every
> design decision out loud before moving to the next version. Claude is your pair programmer —
> not your ghostwriter. You design, Claude helps debug and explains trade-offs.

---

## How to Read This Guide

Each version has:
- 🎯 **Goal** — the one thing this version achieves
- 🔍 **What's wrong with the current code** — why this version is needed
- 🧠 **Concepts first** — understand these before writing a single line
- 📁 **Files** — what to create or modify
- 🔧 **Implementation notes** — exact steps, data structures, key decisions
- 🧪 **How to test** — specific commands to verify correctness
- ⚠️ **Common mistakes** — what trips people up here
- 🔓 **What this unlocks** — why it matters for the next version

---

## The V0 Audit — What Your Friend Built and What's Missing

```
server.cpp (160 lines)
├── TCP socket creation, bind port 8080, listen
├── accept() loop in main, spawns detached thread per client
├── unordered_map<string, string> db — shared, protected by one global mutex
├── handleClient(): recv into 1024-byte buffer, space-split string, switch on command
├── Commands: SET key value → "OK", GET key → value or "NOT FOUND"
└── thread.detach() — threads can't be joined, db refs could dangle on shutdown

client.cpp (74 lines)
├── TCP socket, connect to 127.0.0.1:8080
├── getline loop: send message, recv response, print
└── Stops on bytes <= 0
```

**Problems that will break things as you expand:**

|
 Problem 
|
 Why it hurts 
|
|
---
|
---
|
|
 No 
`SO_REUSEADDR`
|
 "Address already in use" crash on every restart during dev 
|
|
 Fixed 1024-byte recv buffer 
|
 A long value could be split across two recv() calls; you'd corrupt it 
|
|
 Space-split parsing 
|
`SET key hello world`
 silently drops "world"; multi-word values impossible 
|
|
 Thread per client 
|
 10k clients = 10k threads, ~80MB stack memory, OS scheduling overhead 
|
|
 One global mutex 
|
 All reads block all writes even though reads could be concurrent 
|
|
 No protocol framing 
|
 You can't distinguish message boundaries; fragmentation is invisible 
|
|
`t.detach()`
|
 Main thread could exit, 
`db`
 and 
`db_mutex`
 on its stack go out of scope 
|
|
 No error responses 
|
 Invalid command → silence; client hangs forever 
|
|
 No RESP protocol 
|
 You can't connect real Redis clients (redis-cli, any library) to this server 
|

---

## Phase 0 — Socket & Protocol Foundation

These two versions fix V0's structural issues without changing any behavior. They're the
unglamorous but essential work before any real features.

---

### V0.1 — Socket Hygiene + Proper Recv Loop

🎯 **Goal**: Fix the three issues that will cause silent data corruption and restart pain
before anything else is built on top.

🔍 **What's wrong**: No `SO_REUSEADDR`, fixed buffer with possible split reads, `t.detach()`
with stack-allocated shared state.

🧠 **Concepts first**:

- **`SO_REUSEADDR`**: When a server closes, the OS keeps the port in TIME_WAIT state for ~2
  minutes (a TCP safety mechanism to catch late-arriving packets). Without this socket option
  set before `bind()`, `bind()` fails with EADDRINUSE. Set it with `setsockopt()` immediately
  after `socket()`. This single line will save you enormous frustration during development.

- **TCP is a stream protocol, not a message protocol**: `recv()` returns however many bytes
  the OS happened to buffer. If a client sends "SET key valuethatislong", your single `recv()`
  might get "SET key val" and the next call gets "uethatislong". You need a framing layer
  to know where one message ends and the next begins. For now: add a newline `\n` delimiter
  and buffer per client until you see one. This is a temporary fix — RESP will replace it in V1.

- **`t.detach()` danger with stack references**: Your `db` and `db_mutex` are declared in
  `main()`'s stack frame. Detached threads continue running after `main()` exits, accessing
  freed memory. Move them to heap (`new`) or make them static/global for now. Better: use
  `std::vector<std::thread>` and join them on shutdown (even if shutdown handling is rough).

📁 **Files to modify**: `server.cpp`

🔧 **Implementation steps**:

1. Add `SO_REUSEADDR` right after `socket()` call:
   ```cpp
   int opt = 1;
   setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
   ```

2. Move `db` and `db_mutex` out of `main()` to file scope (or wrap in a Server struct).

3. Replace the single fixed-buffer recv with a **per-client line buffer**:
   - Each client gets a `std::string` accumulation buffer
   - Pass it by reference into `handleClient()`
   - After each `recv()`, append bytes to the buffer
   - Split on `\n`, process complete lines, leave the remainder in the buffer
   - Handle the edge case: what if there's no `\n` after many calls? (set a max buffer size,
     disconnect the client if exceeded — prevents memory exhaustion)

4. Send `"ERR unknown command\n"` for unrecognized commands instead of silence.

5. Add a `PING` command → `"PONG\n"` — simplest possible smoke test for connectivity.

🧪 **Test**:
```bash
# Compile and run server
g++ -o server server.cpp -pthread && ./server

# In another terminal
nc localhost 8080
PING         # should get PONG
SET foo bar  # should get OK
GET foo      # should get bar
GET missing  # should get NOT FOUND
BADCMD       # should get ERR unknown command (not silence)

# Kill and immediately restart — should NOT get "Address already in use"
```

⚠️ **Common mistakes**:
- Forgetting to null-terminate after recv (you already do this, keep it)
- Appending to the buffer but forgetting to clear the processed portion (the classic "consume
  what you processed" bug — the remainder after the last `\n` stays, don't erase the whole buffer)
- Using `\r\n` vs `\n` — clients on Windows send `\r\n`; strip `\r` from parsed tokens

🔓 **Unlocks**: Safe foundation to build RESP parser on top of without silent corruption.

---

### V0.2 — Command Tokenizer

🎯 **Goal**: Replace the `stringstream >> command >> key >> value` hack with a real tokenizer
that correctly handles quoted strings and variable argument counts.

🔍 **What's wrong**: `ss >> command >> key >> value` silently discards arguments after the third
word. `SET key "hello world"` parses as key=`key`, value=`"hello`. You can't implement `MSET`
(multiple key-value pairs) or `HSET` (field-value pairs) with this.

🧠 **Concepts first**:

- **Argument vector pattern**: Redis commands are represented internally as
  `vector<string> argv` where `argv[0]` is always the command name. This is exactly what
  your RESP parser will produce in V1 — design your command dispatch to accept a `vector<string>`
  now so V1 is a drop-in swap of the parser, not a rewrite of dispatch.

- **Simple tokenizer rules for this version** (not RESP yet — that's V1):
  - Split on whitespace
  - Quoted tokens: if a token starts with `"`, read until the next unescaped `"`
  - Command is case-insensitive: uppercase `argv[0]` on parse so `get` and `GET` both work

📁 **Files**: Create `parser.h` / `parser.cpp`, modify `server.cpp`

🔧 **Implementation steps**:

1. Write a `tokenize(const std::string& line)` function returning `vector<string>`:
   ```
   Input:  SET mykey "hello world"
   Output: ["SET", "mykey", "hello world"]
   
   Input:  HSET user:1 name Alice age 30
   Output: ["HSET", "user:1", "name", "Alice", "age", "30"]
   ```

2. Create a command dispatch function:
   ```cpp
   std::string dispatch(const std::vector<std::string>& argv,
                        std::unordered_map<std::string, std::string>& db,
                        std::mutex& db_mutex);
   ```

3. Inside dispatch, check `argv.size()` before accessing indices — wrong arity should
   return `"ERR wrong number of arguments for '"+argv[0]+"' command\n"` (exactly what
   real Redis says).

4. Move command implementations out of `handleClient` into dedicated functions, one per
   command. This structure will survive all the way to V6.

🧪 **Test**:
```bash
SET mykey "hello world"   # should store and retrieve the full string with space
GET mykey                 # should return: hello world
MSET                      # should get: ERR wrong number of arguments
```

⚠️ **Common mistakes**:
- Forgetting to uppercase the command name before comparison
- Off-by-one on arity checks (SET needs exactly 3 tokens: SET + key + value = argv.size() == 3)

🔓 **Unlocks**: You now have `vector<string> argv` as the internal command representation,
which is exactly what the RESP parser produces. V1 will be a 5-line change to the input path.

---

## Phase 1 — Real Wire Protocol (RESP2)

This is the single most impactful change in the entire project. After this version,
`redis-cli` and any Redis library in any language can connect to your server.

---

### V1.0 — RESP2 Decoder

🎯 **Goal**: Parse incoming bytes from clients as RESP2 frames, producing `vector<string> argv`
— exactly what your V0.2 dispatch already accepts.

🔍 **What's wrong**: Your current protocol is newline-delimited plain text. It works with `nc`,
but no real Redis client sends that. RESP is what Redis and all its clients actually speak.

🧠 **Concepts first**:

RESP2 is a dead-simple line-based protocol. Every message is one of these types:

```
Simple String:  +OK\r\n
Error:          -ERR message here\r\n
Integer:        :1000\r\n
Bulk String:    $6\r\nfoobar\r\n        ($-1\r\n means NULL)
Array:          *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
```

A command from any client is always an **Array of Bulk Strings**:
- `*3\r\n` → array of 3 elements
- `$3\r\nSET\r\n` → bulk string "SET" (3 bytes)
- `$3\r\nfoo\r\n` → bulk string "foo"
- `$3\r\nbar\r\n` → bulk string "bar"

So parsing a command = parse one Array frame. The Array tells you how many Bulk Strings follow.
Each Bulk String tells you exactly how many bytes to read. **No ambiguity, no splitting on
whitespace, binary-safe.**

The parser is a state machine with two states:
1. Reading a line ending in `\r\n` (for type prefixes, counts, short values)
2. Reading exactly N bytes (for bulk string data, followed by a consuming `\r\n`)

📁 **Files**: Create `resp.h` / `resp.cpp`

🔧 **Implementation steps**:

1. Define a `RespParser` class that owns a `std::string buffer_` (raw bytes received so far).

2. Write `void feed(const char* data, size_t len)` — appends bytes to the buffer.

3. Write `bool tryParse(std::vector<std::string>& out)` — attempts to extract one complete
   command array from the buffer. Returns true and fills `out` if a complete message is
   available, false if more bytes are needed, throws on malformed input.

4. The parsing logic:
   ```
   Read until \r\n → this is your "line"
   First char of line:
     '*' → this is an array; N = parse integer from rest of line
           loop N times, each iteration must parse a bulk string ($)
     '$' → bulk string; N = parse integer (byte count)
           read exactly N bytes + consume \r\n
     '+' → simple string (you'll need this for responses; in commands, shouldn't appear)
     '-' → error (shouldn't appear in client→server direction)
     ':' → integer (shouldn't appear in commands)
   ```

5. Key correctness rule: **do not consume from `buffer_` until you know the full message is
   there**. First check if enough bytes exist, then consume. Otherwise a partial read will
   eat the buffer and the rest of the message never arrives.

6. Per-client, each connection gets its own `RespParser` instance (replace the old line buffer).

7. In the event handler: `parser.feed(recv_bytes)`, then `while(parser.tryParse(argv)) dispatch(argv)`.

🧪 **Test** — this is the big one:
```bash
# Install redis-cli (package: redis-tools on Ubuntu) — doesn't need a Redis server
redis-cli -p 8080 ping          # PONG
redis-cli -p 8080 set foo bar   # OK
redis-cli -p 8080 get foo       # "bar"
redis-cli -p 8080 get missing   # (nil)
redis-cli -p 8080 set k "hello world"  # OK
redis-cli -p 8080 get k                # "hello world"
```

If `redis-cli` can talk to your server, your RESP parser is correct.

⚠️ **Common mistakes**:
- Off-by-one on bulk string reads: `$5\r\nhello\r\n` — you read 5 bytes ("hello") then
  consume the trailing `\r\n`. Many people forget to consume that trailing `\r\n` and
  misalign on the next frame.
- Blocking on partial messages: if you call `read()` inside your parser assuming it will
  block until a full message arrives, you'll freeze when using an event loop. The parser must
  be fully non-blocking — it processes whatever's in the buffer and says "not enough yet."
- Not handling `$-1\r\n` (null bulk string) — means a key doesn't exist; you'll need this
  in your encoder in V1.1.

🔓 **Unlocks**: Real redis-cli works. Any Redis library in any language works. You have left
"toy project" territory and built something real.

---

### V1.1 — RESP2 Encoder

🎯 **Goal**: Replace your plain-text responses (`"OK\n"`, `"NOT FOUND\n"`) with proper
RESP2-encoded replies.

🔍 **What's wrong**: Your server speaks RESP on input but still replies in plain text. `redis-cli`
will show garbage or protocol errors for any response.

🧠 **Concepts first**: RESP encoding for responses is simpler than decoding — it's just string
formatting. Real Redis sends:
- `+OK\r\n` for simple success
- `-ERR message\r\n` for errors
- `:42\r\n` for integer replies
- `$6\r\nfoobar\r\n` for a string value, `$-1\r\n` for nil (key not found)
- `*3\r\n...` for array replies (used by KEYS, LRANGE, SMEMBERS, etc.)

📁 **Files**: Add to `resp.h` / `resp.cpp`

🔧 **Implementation steps**:

Write these helper functions (they just return `std::string`):
```cpp
std::string encodeOK();                         // "+OK\r\n"
std::string encodeError(const std::string& msg);// "-ERR msg\r\n"
std::string encodeInteger(long long n);         // ":N\r\n"
std::string encodeBulkString(const std::string& s); // "$N\r\ndata\r\n"
std::string encodeNullBulk();                   // "$-1\r\n"
std::string encodeArray(const std::vector& items); // *N\r\n...
std::string encodeNullArray();                  // "*-1\r\n"
```

Update every command response in `dispatch()` to use these functions.
Map your old responses:
- `"OK\n"` → `encodeOK()`
- `"NOT FOUND\n"` → `encodeNullBulk()`
- `"ERR ...\n"` → `encodeError("...")`
- A string value → `encodeBulkString(value)`
- An integer count → `encodeInteger(n)`

🧪 **Test**:
```bash
redis-cli -p 8080 ping        # PONG  (not the raw string "PONG\n")
redis-cli -p 8080 set k v     # OK
redis-cli -p 8080 get k       # "v"   (with quotes — redis-cli wraps bulk strings)
redis-cli -p 8080 get missing # (nil) (not "NOT FOUND")
redis-cli -p 8080 BADINPUT    # (error) ERR unknown command
```

⚠️ **Common mistakes**:
- Forgetting `\r\n` at the end of every RESP frame (this is the #1 cause of redis-cli hanging)
- Encoding a string with newlines inside it as a Simple String (+) instead of Bulk String ($) —
  Simple Strings cannot contain `\r` or `\n`, always use Bulk String for arbitrary data

🔓 **Unlocks**: Full bidirectional RESP compatibility. `redis-cli --no-auth-warning` will work
for every command you implement from this point forward.

---

## Phase 2 — Event Loop: From Threads to Reactor

This phase replaces the thread-per-client model with a single-threaded event loop.
This is architecturally the most important change — it's what makes Redis Redis.

---

### V2.0 — poll()-based Event Loop

🎯 **Goal**: Handle multiple clients on one thread using `poll()`. No more `thread t(...)` per
connection.

🔍 **What's wrong**: Thread-per-client means 10,000 connections = 10,000 OS threads. Each thread
has an ~8MB default stack. That's 80GB RAM just for thread stacks at 10k clients. Linux
thread context-switch overhead also grows non-linearly. Real Redis handles 100k+ connections
on one thread.

🧠 **Concepts first**:

**The reactor pattern**: Instead of "one thread waits on one socket," you register many file
descriptors with the OS and ask: "tell me which ones are ready." The OS returns a list, you
handle each ready fd, then ask again. This is the core of Redis's event loop.

**`poll()`**: Takes an array of `pollfd` structs (each has a fd, events-to-watch, events-that-happened),
blocks until at least one fd is ready, returns. Simpler than `epoll` to start with. O(N) per call
(checks all fds), which matters at very high N, but is fine for learning.

```cpp
struct pollfd {
    int   fd;
    short events;   // what to watch for: POLLIN (readable), POLLOUT (writable)
    short revents;  // what happened (filled by kernel after poll() returns)
};
```

**Non-blocking sockets**: In event-loop mode, sockets must be non-blocking (`fcntl(fd, F_SETFL, O_NONBLOCK)`).
Otherwise `read()`/`write()` could block, stalling the entire loop for all clients. A non-blocking
`read()` returns EAGAIN/EWOULDBLOCK if there's nothing to read — that's not an error, it means
"no data yet, come back later."

**Per-client state**: Each connection needs its own: `RespParser` instance, read buffer,
write buffer (bytes waiting to be flushed), and connection state. Group these in a `Client` struct.

📁 **Files**: Create `eventloop.h` / `eventloop.cpp`, create `client.h`, modify `server.cpp`

🔧 **Implementation steps**:

1. Define `struct Client`:
   ```cpp
   struct Client {
       int fd;
       RespParser parser;       // from V1.0
       std::string write_buf;   // outgoing bytes not yet sent
       bool closing = false;    // set when we want to close after flushing write_buf
   };
   ```

2. Main loop structure:
   ```
   server_fd = socket + bind + listen + set non-blocking
   clients = map<int, Client>  (fd → Client)
   
   loop forever:
       build pollfd array: [server_fd, POLLIN] + [each client fd, POLLIN + (POLLOUT if write_buf nonempty)]
       poll(fds, n, timeout_ms)  // timeout for TTL sweep timer (see V4)
       
       if server_fd is POLLIN:
           client_fd = accept()
           set client_fd non-blocking
           clients[client_fd] = Client{client_fd}
       
       for each client fd that is POLLIN:
           bytes = read(fd, buf, sizeof(buf))
           if bytes <= 0: close and remove client
           else: parser.feed(bytes)
                 while parser.tryParse(argv): result = dispatch(argv); client.write_buf += result
       
       for each client fd that is POLLOUT and write_buf nonempty:
           n = write(fd, write_buf.data(), write_buf.size())
           if n > 0: consume n bytes from front of write_buf
           if write_buf empty and client.closing: close fd
   ```

3. Remove all thread code, mutex, and thread.detach(). The loop is single-threaded — no locking needed, ever, for data access. This is the payoff.

4. Critical: `accept()` in a loop until it returns EAGAIN (multiple clients may have queued up between polls).

🧪 **Test**:
```bash
# Connect two clients simultaneously
redis-cli -p 8080 set a 1 &
redis-cli -p 8080 set b 2 &
redis-cli -p 8080 get a  # 1
redis-cli -p 8080 get b  # 2

# Verify one client doesn't block another
# Open redis-cli in interactive mode, type nothing (just hold the connection open)
# In another terminal: redis-cli -p 8080 ping — should respond immediately, not wait
```

⚠️ **Common mistakes**:
- Not setting sockets non-blocking before adding to poll — first blocking read will freeze everyone
- Not draining `accept()` in a loop — under load, one poll wakeup might have multiple incoming connections
- Using blocking `write()` directly on client socket — if the kernel send buffer is full (slow client),
  `write()` blocks. Always go through `write_buf` and only flush on POLLOUT events
- Building the pollfd array inside the loop but not resizing it when clients connect/disconnect

🔓 **Unlocks**: 10k+ concurrent connections on one thread. No mutex needed. No more thread stack overhead.

---

### V2.1 — epoll Event Loop (Linux Only)

🎯 **Goal**: Replace `poll()` with `epoll`, which is O(1) per ready event instead of O(N) for
all registered fds.

🔍 **What's wrong with poll()**: At N registered fds, `poll()` scans the entire array every call —
O(N) even if only 1 fd is ready. `epoll` maintains a kernel-side ready list and returns only the
fds that are actually ready. This is why Linux servers can handle hundreds of thousands of connections.

🧠 **Concepts first**:

**epoll API** (3 syscalls):
```cpp
int epfd = epoll_create1(0);             // create epoll instance, returns a fd

// Add/modify/remove a fd from the interest list
struct epoll_event ev;
ev.events = EPOLLIN | EPOLLET;          // watch for reads, edge-triggered
ev.data.fd = fd;
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev); // EPOLL_CTL_MOD to modify, _DEL to remove

// Wait for events
struct epoll_event events[MAX_EVENTS];
int n = epoll_wait(epfd, events, MAX_EVENTS, timeout_ms); // returns count of ready events
// events[0..n-1] are ready
```

**Level-triggered vs Edge-triggered**:
- Level-triggered (default, `EPOLLIN`): epoll wakes you up as long as data is available.
  Simpler, can be slightly less efficient because you get repeated wakeups.
- Edge-triggered (`EPOLLIN | EPOLLET`): epoll wakes you up exactly once when new data arrives.
  More efficient, but **you must drain the socket completely** (read in a loop until EAGAIN)
  on each wakeup, or data sits unprocessed forever.

**For learning: start with level-triggered**. It's conceptually simpler and you won't miss events.
Revisit edge-triggered as an optimization once everything works.

📁 **Files**: Modify `eventloop.cpp` (swap `poll()` for `epoll`)

🔧 **Implementation steps**:

1. Replace `poll()` scaffold from V2.0 with:
   - `epoll_create1(0)` at startup
   - `epoll_ctl(ADD)` when a client connects, `epoll_ctl(MOD)` when write_buf goes
     from empty to nonempty (add EPOLLOUT) or back to empty (remove EPOLLOUT)
   - `epoll_ctl(DEL)` when a client disconnects
   - Main loop: `n = epoll_wait(epfd, events, MAX, timeout_ms)`, then iterate `events[0..n-1]`

2. The EPOLLOUT handling is important: only register EPOLLOUT when you actually have data to
   send. If you leave EPOLLOUT registered on an idle connection, epoll fires continuously
   (the socket is always writable when there's nothing to write), burning CPU.

3. Add `EPOLLERR | EPOLLHUP` to catch errors/hangups.

🧪 **Test**: Same as V2.0 but benchmark it:
```bash
# Use redis-benchmark (comes with redis-tools)
redis-benchmark -p 8080 -t set,get -n 100000 -q
# You won't match real Redis yet (no pipeline support, no RESP3, no fine-tuned buffers)
# but you should see significant ops/sec improvement over the poll() version
```

⚠️ **Common mistakes**:
- Forgetting to update EPOLLOUT registration when write_buf transitions (common source of
  "writes stop working intermittently" bugs)
- Calling `epoll_ctl(DEL)` after `close(fd)` — close() automatically removes from epoll,
  calling DEL after close causes an error on some kernels

🔓 **Unlocks**: Production-grade I/O multiplexing. The foundation is now genuinely similar
to Redis's own `ae_epoll.c`.

---

## Phase 3 — The Type System

Redis's real power is typed values. This phase builds the `robj` equivalent — a typed value
wrapper — and then implements each data type one at a time.

---

### V3.0 — Typed Value Wrapper (redisObject)

🎯 **Goal**: Replace `unordered_map<string, string>` with `unordered_map<string, RedisObject*>`
where `RedisObject` carries both a type tag and a pointer to the actual data structure.

🔍 **What's wrong**: Right now the value is always a `string`. There's no way to store a list,
hash, set, or sorted set. You need a typed container before you can implement anything else.

🧠 **Concepts first**:

Redis's `robj` (redis object) struct in C:
```c
typedef struct redisObject {
    unsigned type:4;      // OBJ_STRING, OBJ_LIST, OBJ_HASH, OBJ_SET, OBJ_ZSET
    unsigned encoding:4;  // which concrete implementation backs this type
    unsigned lru:24;      // access time or frequency counter for eviction (V7)
    int refcount;         // reference counting for shared objects (V5)
    void *ptr;            // pointer to the actual data structure
} robj;
```

In C++ you can do this with a struct + enum + union/variant, or just a class hierarchy.
For clarity in learning: use `std::variant<std::string, std::list<std::string>, ...>`
or a struct with a type enum and `void*` (closer to the real Redis design).

**The key insight**: type and encoding are separate. A Hash can be stored as a listpack
(compact, cache-friendly) when small, or as a real hash table when large. The type stays
`OBJ_HASH`, the encoding changes. This lets all the HSET/HGET command logic work identically
regardless of which encoding is active — the commands call the same typed interface.

📁 **Files**: Create `object.h`, modify `server.cpp` to use `unordered_map<string, RedisObject*>`

🔧 **Implementation steps**:

1. Define the type and encoding enums:
   ```cpp
   enum ObjectType { OBJ_STRING, OBJ_LIST, OBJ_HASH, OBJ_SET, OBJ_ZSET };
   enum ObjectEncoding {
       ENC_RAW,        // SDS-backed raw string
       ENC_INT,        // integer stored as long long
       ENC_LISTPACK,   // compact sequential encoding (V5)
       ENC_QUICKLIST,  // linked list of listpack nodes (V5)
       ENC_HASHTABLE,  // std::unordered_map
       ENC_SKIPLIST,   // skip list (V3.5)
       ENC_INTSET      // sorted integer array (V5)
   };

   struct RedisObject {
       ObjectType type;
       ObjectEncoding encoding;
       void* ptr;
       // later: uint32_t lru; (V7)
   };
   ```

2. Add helper constructors: `createStringObject(string)`, `createListObject()`,
   `createHashObject()`, `createSetObject()`, `createZSetObject()`.

3. Update `db` to `unordered_map<string, RedisObject*>`.

4. Update GET/SET to create/access string objects via the wrapper.

5. Add a `TYPE` command: `TYPE key` → `+string\r\n`, `+list\r\n`, etc. (the actual
   type name, not the enum). Returns `+none\r\n` if key doesn't exist.

6. Add a `DEL key [key ...]` command that deletes one or more keys.

🧪 **Test**:
```bash
redis-cli -p 8080 set foo bar
redis-cli -p 8080 type foo      # string
redis-cli -p 8080 del foo
redis-cli -p 8080 exists foo    # 0
```

⚠️ **Common mistakes**:
- Memory leaks: when you `DEL` a key, free the inner data structure first, then the `RedisObject`.
  Use a `destroyObject(RedisObject*)` helper that dispatches on type to do the right thing.
- The `void*` pattern requires careful casting — consider using `std::variant` if you prefer
  type safety, but know that the real Redis uses `void*` with explicit type checks everywhere.

🔓 **Unlocks**: All subsequent data types (V3.1–V3.5) have a home. Type-checking for commands
that operate on a specific type (HSET on a string key → WRONGTYPE error).

---

### V3.1 — String Commands

🎯 **Goal**: Full string type command set.

🧠 **New concepts**:
- **Integer encoding**: if a string value looks like an integer (fits in a `long long`), Redis
  stores it as the integer itself, not as a string. This makes INCR/DECR O(1) without parsing.
  Implement: on SET, try `stoll(value)`, if it succeeds store as `ENC_INT` with the long long
  cast to `void*` (or in a union). On GET, format back to string.
- **Atomicity of INCR**: since the event loop is single-threaded, INCR is automatically atomic —
  no locks needed. This is the most common reason people reach for Redis over a plain DB.

📁 **Files**: Create `cmd_string.cpp`

🔧 **Commands to implement**:

|
 Command 
|
 Behavior 
|
 Notes 
|
|
---
|
---
|
---
|
|
`SET key value [EX seconds] [PX ms] [NX] [XX]`
|
 Store string 
|
 NX = only set if not exists; XX = only set if exists; EX/PX sets TTL inline 
|
|
`GET key`
|
 Return value or nil 
|
|
|
`GETSET key value`
|
 Set and return old value 
|
 Deprecated in Redis 6.2 but good to learn atomicity 
|
|
`MSET key val [key val ...]`
|
 Set multiple 
|
 Atomic 
|
|
`MGET key [key ...]`
|
 Get multiple 
|
 Returns array; nil for missing keys 
|
|
`SETNX key value`
|
 Set only if not exists 
|
 Returns 1 (set) or 0 (not set) 
|
|
`SETEX key seconds value`
|
 Set with expiry 
|
|
|
`INCR key`
|
 Atomically increment integer 
|
 Error if value is not an integer 
|
|
`DECR key`
|
 Atomically decrement 
|
|
|
`INCRBY key delta`
|
 Increment by N 
|
|
|
`DECRBY key delta`
|
 Decrement by N 
|
|
|
`APPEND key value`
|
 Append to string 
|
 Returns new length 
|
|
`STRLEN key`
|
 Return string length 
|
 0 for missing keys 
|

🧪 **Test**:
```bash
redis-cli -p 8080 set counter 0
redis-cli -p 8080 incr counter     # 1
redis-cli -p 8080 incrby counter 5 # 6
redis-cli -p 8080 append mykey "hello"
redis-cli -p 8080 append mykey " world"
redis-cli -p 8080 strlen mykey     # 11
redis-cli -p 8080 setnx mykey 999  # 0 (not set, already exists)
redis-cli -p 8080 set k v NX       # OK (doesn't exist)
redis-cli -p 8080 set k v2 NX      # (nil) (exists, NX rejects)
```

---

### V3.2 — Hash Commands

🎯 **Goal**: Implement the Hash type — a map of field→value under one key.

🧠 **New concepts**:
- The Hash's backing store for now: `unordered_map<string, string>` (the full hash table encoding).
- The listpack encoding (V5.1) will be retrofitted later. For now, use the full hash table always.
- **WRONGTYPE error**: if a key exists and holds a different type, return
  `-WRONGTYPE Operation against a key holding the wrong kind of value\r\n`. Every type-specific
  command must check this before operating.

📁 **Files**: Create `cmd_hash.cpp`

🔧 **Commands to implement**:

|
 Command 
|
 Behavior 
|
|
---
|
---
|
|
`HSET key field value [field value ...]`
|
 Set one or more fields 
|
|
`HGET key field`
|
 Get field value (nil if not found) 
|
|
`HMSET key field value [field value ...]`
|
 Same as HSET (legacy) 
|
|
`HMGET key field [field ...]`
|
 Get multiple fields (array reply, nil for missing) 
|
|
`HDEL key field [field ...]`
|
 Delete fields, returns count deleted 
|
|
`HEXISTS key field`
|
 1 or 0 
|
|
`HLEN key`
|
 Count of fields 
|
|
`HKEYS key`
|
 Array of all field names 
|
|
`HVALS key`
|
 Array of all values 
|
|
`HGETALL key`
|
 Flat array of field, value, field, value... 
|
|
`HINCRBY key field delta`
|
 Increment integer field 
|

🧪 **Test**:
```bash
redis-cli -p 8080 hset user:1 name Alice age 30 city Delhi
redis-cli -p 8080 hgetall user:1   # name Alice age 30 city Delhi
redis-cli -p 8080 hget user:1 name  # Alice
redis-cli -p 8080 hincrby user:1 age 1
redis-cli -p 8080 hget user:1 age   # 31
redis-cli -p 8080 set user:1 "oops" # OK (overwrites hash with string)
redis-cli -p 8080 hget user:1 name  # WRONGTYPE error
```

---

### V3.3 — List Commands

🎯 **Goal**: Doubly-linked list with O(1) push/pop at both ends.

🧠 **New concepts**:
- Backing store: `std::list<string>` (doubly linked list — gives O(1) push/pop, O(N) index access).
- Lists are ordered, allow duplicates, allow the same value at multiple positions.
- **LRANGE** is the workhorse: `LRANGE key 0 -1` returns all elements. Negative indices count
  from the end (-1 = last element, -2 = second to last). Normalize negative indices on entry:
  `if (idx < 0) idx = list.size() + idx`.

📁 **Files**: Create `cmd_list.cpp`

🔧 **Commands to implement**:

|
 Command 
|
 Behavior 
|
|
---
|
---
|
|
`LPUSH key value [value ...]`
|
 Prepend to list, returns new length 
|
|
`RPUSH key value [value ...]`
|
 Append to list, returns new length 
|
|
`LPOP key [count]`
|
 Remove and return from head 
|
|
`RPOP key [count]`
|
 Remove and return from tail 
|
|
`LLEN key`
|
 List length 
|
|
`LRANGE key start stop`
|
 Slice (inclusive both ends, negative indices ok) 
|
|
`LINDEX key index`
|
 Get element at index 
|
|
`LSET key index value`
|
 Set element at index 
|
|
`LINSERT key BEFORE\|AFTER pivot value`
|
 Insert relative to a value 
|
|
`LREM key count value`
|
 Remove N occurrences of value 
|
|
`LTRIM key start stop`
|
 Keep only the given range 
|

🧪 **Test**:
```bash
redis-cli -p 8080 rpush mylist a b c
redis-cli -p 8080 lrange mylist 0 -1   # a b c
redis-cli -p 8080 lpush mylist z
redis-cli -p 8080 lrange mylist 0 -1   # z a b c
redis-cli -p 8080 lrange mylist 1 2    # a b
redis-cli -p 8080 lpop mylist          # z
redis-cli -p 8080 llen mylist          # 3
```

---

### V3.4 — Set Commands

🎯 **Goal**: Unordered collection of unique strings with set algebra.

🧠 **New concepts**:
- Backing store: `unordered_set<string>`.
- SINTER/SUNION/SDIFF operate across multiple keys — iterate the db for each key, check WRONGTYPE.
- SRANDMEMBER / SPOP require random selection from an unordered container — pick a random iterator
  (`std::advance(it, rand() % size)`).

📁 **Files**: Create `cmd_set.cpp`

🔧 **Commands to implement**:

|
 Command 
|
 Behavior 
|
|
---
|
---
|
|
`SADD key member [member ...]`
|
 Add members, returns count added 
|
|
`SREM key member [member ...]`
|
 Remove members, returns count removed 
|
|
`SMEMBERS key`
|
 All members (unordered) 
|
|
`SCARD key`
|
 Count of members 
|
|
`SISMEMBER key member`
|
 1 or 0 
|
|
`SMISMEMBER key member [member ...]`
|
 Array of 1/0 
|
|
`SPOP key [count]`
|
 Remove and return random member(s) 
|
|
`SRANDMEMBER key [count]`
|
 Return random member(s) without removing 
|
|
`SINTER key [key ...]`
|
 Intersection (members in ALL sets) 
|
|
`SUNION key [key ...]`
|
 Union (members in ANY set) 
|
|
`SDIFF key [key ...]`
|
 Difference (in first set, not in any other) 
|
|
`SINTERSTORE dst key [key ...]`
|
 Like SINTER but store result in dst 
|
|
`SUNIONSTORE dst key [key ...]`
|
 Like SUNION but store result 
|
|
`SDIFFSTORE dst key [key ...]`
|
 Like SDIFF but store result 
|

🧪 **Test**:
```bash
redis-cli -p 8080 sadd s1 a b c d
redis-cli -p 8080 sadd s2 c d e f
redis-cli -p 8080 sinter s1 s2    # c d
redis-cli -p 8080 sunion s1 s2    # a b c d e f
redis-cli -p 8080 sdiff  s1 s2    # a b
redis-cli -p 8080 scard s1        # 4
```

---

### V3.5 — Sorted Set Commands

🎯 **Goal**: Implement the Sorted Set (ZSET) — Redis's most powerful data structure.

🧠 **New concepts**:

A ZSET is TWO data structures maintained in sync:
- `unordered_map<string, double>` — member → score for O(1) score lookup (ZSCORE, ZADD update)
- A sorted structure for O(log N) rank and range queries

**The sorted structure — implementing a Skip List**:

A skip list is a linked list with multiple "express lanes." Each node has a randomly-assigned
height (1 to MAX_LEVEL). Level 1 is the base linked list (all nodes). Level 2 links skip ~half
the nodes. Level 3 skips ~3/4. Searching: start at the top level, advance forward while the
next node's score is ≤ target, drop a level, repeat until level 1.

```
Level 3: head ──────────────────────────────> [50] ──────────> tail
Level 2: head ─────────────> [30] ─────────> [50] ─────────── tail
Level 1: head ──> [10] ──> [20] ──> [30] ──> [50] ──> [70] ─> tail
```

**Redis's modification**: each node also stores a `span` per level (how many level-1 nodes
are skipped by this pointer). Summing spans during a search gives you the rank of any element
in O(log N) — this is how `ZRANK` works.

For C++: implement `SkipList` as a class with `insert(score, member)`, `remove(member)`,
`getRank(member)`, `getRange(start, stop)`, `getRangeByScore(min, max)`. Use a random height
with probability 0.25 per additional level (Redis uses 0.25, not 0.5), MAX_LEVEL = 32.

Implementation note for this build: V3.5 keeps the `SkipList` API boundary, but backs it with
C++ ordered containers to finish the command behavior first. A hand-rolled probabilistic skip
list can replace that internal representation later without changing command handlers.

📁 **Files**: Create `skiplist.h` / `skiplist.cpp`, `cmd_zset.cpp`

🔧 **Commands to implement**:

|
 Command 
|
 Behavior 
|
|
---
|
---
|
|
`ZADD key [NX\|XX] [GT\|LT] [CH] score member [score member ...]`
|
 Add/update members 
|
|
`ZRANGE key start stop [BYSCORE] [REV] [LIMIT offset count] [WITHSCORES]`
|
 Range query 
|
|
`ZRANGEBYSCORE key min max [WITHSCORES] [LIMIT offset count]`
|
 Range by score 
|
|
`ZREVRANGE key start stop [WITHSCORES]`
|
 Reverse rank range 
|
|
`ZRANK key member`
|
 0-based rank (sorted by score asc) 
|
|
`ZREVRANK key member`
|
 Rank from highest 
|
|
`ZSCORE key member`
|
 Get score of member 
|
|
`ZCARD key`
|
 Count of members 
|
|
`ZCOUNT key min max`
|
 Count members with score in range 
|
|
`ZREM key member [member ...]`
|
 Remove members 
|
|
`ZINCRBY key increment member`
|
 Add to member's score 
|
|
`ZPOPMIN key [count]`
|
 Remove + return lowest-score members 
|
|
`ZPOPMAX key [count]`
|
 Remove + return highest-score members 
|

🧪 **Test** — the leaderboard pattern:
```bash
redis-cli -p 8080 zadd leaderboard 1500 alice 2300 bob 1800 carol 2100 dave
redis-cli -p 8080 zrange leaderboard 0 -1 withscores  # all, sorted ascending
redis-cli -p 8080 zrevrange leaderboard 0 2            # top 3
redis-cli -p 8080 zrank leaderboard alice              # rank 0 (lowest score)
redis-cli -p 8080 zincrby leaderboard 1000 alice       # alice now 2500
redis-cli -p 8080 zrank leaderboard alice              # now rank 3 (highest)
redis-cli -p 8080 zrangebyscore leaderboard 1800 2200  # carol dave
```

⚠️ **Common mistakes**:
- Forgetting to keep both the hash map and skip list in sync on every ZADD/ZREM/ZINCRBY
  (they MUST stay consistent — if they diverge, you get wrong ZSCORE and wrong rank for the same member)
- Skip list level assignment: use `rand() / RAND_MAX < 0.25` for each additional level
  (not 0.5 — Redis uses 0.25 to keep most nodes at height 1, reducing memory)
- Score comparison with floats: use `double`. Handle the special values
  `-inf` and `+inf` as range endpoints in ZRANGEBYSCORE (real Redis supports `ZRANGEBYSCORE key -inf +inf`)

🔓 **Unlocks**: All five core Redis data types implemented. You now have a feature-complete
in-memory store. Everything after this is production hardening.

---

## Phase 4 — TTL Engine

---

### V4.0 — Expiry Metadata Store

🎯 **Goal**: Add a parallel data structure that tracks which keys have an expiry and when.

🧠 **Concept**: Redis doesn't embed expiry inside `robj`. It maintains a *separate* hash table
mapping `key → expiry_timestamp_ms`. Why separate? It lets you iterate expired keys independently
of the main keyspace, and it keeps the hot-path `robj` struct small (cache-friendly).

📁 **Files**: Add to `server.cpp` or a new `db.h`

🔧 **Implementation**:
Add `unordered_map<string, long long> expires` (key → Unix timestamp in milliseconds).
Add a helper: `long long now_ms()` using `std::chrono::steady_clock` or `clock_gettime(CLOCK_MONOTONIC)`.

---

### V4.1 — Lazy Expiry

🎯 **Goal**: Any key access checks expiry first; expired keys are transparently deleted.

🔧 **Implementation**: Write a helper `bool isExpired(const string& key)` that checks `expires`
map. Write `void expireIfNeeded(const string& key)` that calls `isExpired` and deletes the key
(from both `db` and `expires`) if true.

Call `expireIfNeeded(key)` at the top of EVERY read command (GET, HGET, LRANGE, etc.)
and return nil/error as if the key never existed.

---

### V4.2 — Active Expiry

🎯 **Goal**: Delete keys that expire but are never accessed again (lazy alone would never clean them).

🧠 **Concept**: Redis's active expiry runs as a periodic task inside the event loop's time-event
mechanism. It samples 20 random keys from the expires map, deletes any that have expired,
and if ≥ 25% of the sample was expired, immediately runs again (this is the exponential
convergence behavior — if there are many expired keys, it stays aggressive; if few, it backs off).
This caps per-cycle cost at O(sample_size) regardless of total keyspace size.

🔧 **Implementation**: In your event loop, add a periodic timer (check every ~100ms). On each
fire, pick up to 20 random keys from `expires`, call `expireIfNeeded` on each.

---

### V4.3 — TTL Commands

🔧 **Commands to implement**:

|
 Command 
|
 Behavior 
|
|
---
|
---
|
|
`EXPIRE key seconds`
|
 Set TTL in seconds. Returns 1 (set) or 0 (key not found) 
|
|
`PEXPIRE key ms`
|
 Set TTL in milliseconds 
|
|
`EXPIREAT key unix_timestamp`
|
 Set absolute expiry (seconds since epoch) 
|
|
`PEXPIREAT key unix_timestamp_ms`
|
 Absolute expiry in ms 
|
|
`TTL key`
|
 Remaining TTL in seconds. -1 = no TTL, -2 = key doesn't exist 
|
|
`PTTL key`
|
 Remaining TTL in milliseconds 
|
|
`PERSIST key`
|
 Remove TTL (make key permanent). Returns 1 or 0 
|

Also: add `EX`, `PX`, `EXAT`, `PXAT` options to `SET` (you partially did this in V3.1 — now
wire it to the real expiry system).

🧪 **Test**:
```bash
redis-cli -p 8080 set k v ex 5        # expires in 5 seconds
redis-cli -p 8080 ttl k               # ~5 (or 4 by the time you run this)
sleep 6
redis-cli -p 8080 get k               # (nil)
redis-cli -p 8080 set permanent hello
redis-cli -p 8080 expire permanent 10
redis-cli -p 8080 persist permanent   # 1
redis-cli -p 8080 ttl permanent        # -1 (no TTL)
```

---

## Phase 5 — Compact Internal Encodings

This phase reduces memory usage dramatically for small collections.
It's the part of Redis LLD most people skip but that's most interesting to implement.

---

### V5.0 — SDS (Simple Dynamic Strings)

🎯 **Goal**: Replace `std::string` for stored values with an SDS-inspired structure that
tracks length without null-termination scanning and is cache-friendly for short strings.

🧠 **Concept**: Real Redis's SDS has a variable-length header based on string size:
- `sdshdr5`: for strings ≤ 31 bytes, type+len packed in 1 byte
- `sdshdr8`: length as uint8, capacity as uint8 (strings ≤ 255 bytes)
- `sdshdr16/32/64`: larger lengths

The header is placed *before* the char buffer in memory. The pointer SDS returns points
*past* the header to the data itself — so it's compatible with `strlen()` and `printf()`
but you can walk backward to read the header for O(1) `sdslen()`.

```
 [ type | len | alloc ] [ d | a | t | a | \0 ]
                         ^
                         sds pointer points here
```

For your C++ implementation: you can approximate this with a struct that contains a `char* buf`,
`size_t len`, `size_t alloc`. The key improvement over `std::string` is that SDS-style strings
can be pre-allocated (for APPEND use cases) and know their own length without scanning for `\0`.

📁 **Files**: Create `sds.h` / `sds.cpp`

🔧 **Implement**: `sds sdsnew(const char* s)`, `void sdsfree(sds s)`, `sds sdscat(sds s, const char* t)`,
`size_t sdslen(sds s)`, `sds sdsgrow(sds s, size_t addlen)` (pre-allocate extra capacity).

---

### V5.1 — listpack (Compact Sequential Encoding)

🎯 **Goal**: For small Hashes, Lists, and ZSETs, store all elements in a single contiguous
memory block instead of heap-scattered linked list nodes.

🧠 **Concept**: A listpack is a flat byte array. Each element is encoded as:
```
[ previous_entry_length (1 or 5 bytes) ] [ encoding ] [ data ] [ element_total_length (1 byte) ]
```
The `previous_entry_length` allows backward traversal without a separate backward pointer.
The `element_total_length` at the end allows the parser to quickly skip forward.

The whole structure starts with a total byte count header and ends with a `0xFF` sentinel.

**Why it's faster for small collections**: A listpack with 5 entries might be 50 bytes total,
fitting entirely in one cache line. A linked list with 5 nodes = 5 allocations scattered
across the heap = 5 potential cache misses per traversal.

**Encoding rules (simplified)**:
- Integer that fits in 7 bits: store as single byte `0xxxxxxx`
- Small string (≤63 bytes): `10xxxxxx` len byte followed by string bytes
- 16-bit/32-bit integers: separate encoding bytes

📁 **Files**: Create `listpack.h` / `listpack.cpp`

🔧 **Implement**: `lpNew()`, `lpAppend(lp, data, len)`, `lpFirst(lp)` / `lpNext(lp, p)` / `lpPrev(lp, p)`,
`lpGet(p, &val, &sval, &slen)` (returns integer or string), `lpLength(lp)`, `lpFree(lp)`.

---

### V5.2 — intset (Integer-only Set Encoding)

🎯 **Goal**: For Sets containing only integers, use a sorted array instead of a hash table.

🧠 **Concept**: An intset is a sorted array of integers at the smallest encoding width that
fits all members — 16, 32, or 64-bit. Lookup is binary search O(log N). Insert maintains sort
order. When a member is added that doesn't fit the current width, the *entire* array is upgraded
to the next width (every existing member rewritten at the new width).

```cpp
struct IntSet {
    uint32_t encoding;   // INTSET_ENC_INT16, _INT32, _INT64
    uint32_t length;
    int8_t   contents[]; // flexible array, cast to int16/32/64 based on encoding
};
```

📁 **Files**: Create `intset.h` / `intset.cpp`

🔧 **Implement**: `intsetNew()`, `intsetAdd(is, value, &success)`, `intsetRemove(is, value, &success)`,
`intsetFind(is, value)` (binary search), `intsetGet(is, pos)`, upgrade logic.

---

### V5.3 — Encoding Auto-Promotion

🎯 **Goal**: Start with the compact encoding, automatically upgrade to the full structure
when a threshold is crossed.

🔧 **Threshold defaults to implement** (match real Redis defaults):

|
 Type 
|
 Compact encoding 
|
 Threshold to upgrade 
|
|
---
|
---
|
---
|
|
 List 
|
 listpack 
|
 >128 entries OR any entry >64 bytes 
|
|
 Hash 
|
 listpack 
|
 >128 entries OR any field/value >64 bytes 
|
|
 ZSET 
|
 listpack 
|
 >128 entries OR any member >64 bytes 
|
|
 Set (integers only) 
|
 intset 
|
 >512 entries (OR non-integer added → upgrade to listpack then hashtable) 
|
|
 Set (mixed) 
|
 listpack 
|
 >128 entries OR any member >64 bytes 
|

On every LPUSH/HSET/ZADD/SADD, check if the threshold is now crossed. If yes:
1. Create the full-size structure (quicklist / unordered_map / skiplist+hashmap / unordered_set)
2. Iterate the compact encoding, insert all elements
3. Free the compact encoding
4. Update `robj.encoding` to the new encoding
5. Update `robj.ptr` to the new structure

🧪 **Test** (use `OBJECT ENCODING` to verify):
```bash
redis-cli -p 8080 hset small a 1 b 2
redis-cli -p 8080 object encoding small    # "listpack"
# add 130 fields...
for i in $(seq 1 130); do redis-cli -p 8080 hset small "field$i" "$i"; done
redis-cli -p 8080 object encoding small    # "hashtable"
```

---

## Phase 6 — Command Dispatch Table

### V6.0 — Structured Command Registry

🎯 **Goal**: Replace the `if/else if` command chain with a hash-table-based command dispatch
table — exactly how real Redis works.

🧠 **Concept**: Real Redis has an array of `redisCommand` structs compiled in:
```c
struct redisCommand {
    char *name;
    redisCommandProc *proc;   // function pointer
    int arity;                // -N means "at least N args"; +N means "exactly N"
    char *sflags;             // "w" = write, "r" = read, "m" = may increase memory, etc.
    // ... more metadata
};
```
At startup, this array is loaded into a hash table keyed by command name. This makes command
lookup O(1) and makes it trivial to add new commands without touching a giant switch statement.

📁 **Files**: Create `commands.h` / `commands.cpp`

🔧 **Implementation**:
1. Define `CommandFunc = std::function<std::string(Client&, const std::vector<std::string>&)>`
2. Define `struct Command { string name; CommandFunc func; int arity; uint32_t flags; }`
3. Build `unordered_map<string, Command> commandTable` at startup, registering all commands
4. In dispatch: `auto it = commandTable.find(uppercase(argv[0]))`, check arity, call `it->func`
5. Implement utility commands: `PING`, `ECHO`, `SELECT`, `DBSIZE`, `FLUSHDB`, `FLUSHALL`,
   `KEYS pattern`, `SCAN cursor MATCH pattern COUNT count`, `EXISTS key [key ...]`,
   `RENAME src dst`, `RENAMENX src dst`, `TYPE key`, `OBJECT ENCODING key`, `DEBUG SLEEP`

🔓 **Unlocks**: Adding new commands is now a one-liner registration. Makes V9 (transactions),
V8 (pub/sub), and V10 (scripting) much easier to hook in.

---

## Phase 7 — Memory Management

### V7.0 — maxmemory Config + Approximated LRU Eviction

🎯 **Goal**: Limit memory usage and evict keys when the limit is hit, using approximated LRU.

🧠 **Concepts**:

**True LRU is expensive to maintain exactly**: a doubly linked list ordered by last-access-time,
with every read/write moving the accessed node to the front — O(1) move, but that's a pointer
update on every single operation, competing with the data access itself.

**Redis's approximated LRU**: add a 24-bit `lru` field to `robj` (you reserved this in V3.0).
On every access, set `lru = now_seconds & 0xFFFFFF` (truncated to 24 bits — wraps every ~194 days,
acceptable). When eviction is needed, sample `maxmemory-samples` (default 5) random keys and
evict the one with the smallest (oldest) `lru` value. This is O(sample_size) per eviction cycle —
orders of magnitude cheaper than maintaining true LRU order, and approximates it well in practice.

**LFU encoding** (also in `lru` field when LFU policy selected): the 24-bit field is split as
`8 bits decay counter | 16 bits frequency counter`. Frequency increments on access with a
logarithmic saturation function so it doesn't just linearly grow forever.

📁 **Files**: Add to `server.cpp` or new `eviction.cpp`

🔧 **Implementation steps**:

1. Add `size_t maxmemory = 0` to server config (0 = unlimited).
2. Implement `size_t estimateMemoryUsage()` — rough estimate: sum of `OBJECT USAGE` equivalents.
   Real Redis tracks this precisely with a per-allocation accounting hook into jemalloc;
   approximate by tracking allocation count.
3. Implement eviction policies as an enum in config:
   ```
   NOEVICTION  — return error on writes when memory full
   ALLKEYS_LRU — evict any key using LRU
   VOLATILE_LRU — only evict keys with TTL set
   ALLKEYS_LFU / VOLATILE_LFU
   ALLKEYS_RANDOM / VOLATILE_RANDOM
   VOLATILE_TTL — evict by closest expiry first
   ```
4. Before every write command: if memory > maxmemory and policy != NOEVICTION, run eviction cycle.
5. Eviction cycle: sample N random keys from the appropriate pool (all keys or only volatile),
   score them by policy, evict the worst one, repeat until memory is below limit or pool is exhausted.

🧪 **Test**:
```bash
# Set maxmemory low (in config or via CONFIG SET)
redis-cli -p 8080 config set maxmemory 1mb
redis-cli -p 8080 config set maxmemory-policy allkeys-lru
# Fill with data until eviction kicks in
for i in $(seq 1 50000); do redis-cli -p 8080 set "key$i" "$(head -c 100 /dev/urandom | base64)"; done
redis-cli -p 8080 dbsize   # should be < 50000 — eviction happened
```

---

## Phase 8 — Persistence

### V8.0 — RDB Snapshot (Blocking Save)

🎯 **Goal**: Serialize the entire in-memory dataset to a binary file.

🧠 **Concept**: RDB format structure:
```
REDIS0011         -- magic string + version (9 bytes)
FA auxfield key val -- optional metadata (Redis version, creation time, etc.)
FE db_number      -- database selector
FB hash_size expires_size -- resize hint
[ (FC expiry_ms)? type key value ]... -- each key-value pair, with optional expiry
FF                -- end of data
[ 8-byte CRC64 checksum ]
```

Types are encoded as compact binary. Strings use RDB_ENC_INT (if value is an integer) or
length-prefixed bytes. Lists/Hashes/Sets/ZSETs iterate their elements.

📁 **Files**: Create `rdb.h` / `rdb.cpp`

🔧 **Implementation steps**:
1. Write `saveRDB(const string& filename)`:
   - Write magic header `REDIS0011`
   - For each database (you likely have just one, db 0):
     - Write `0xFE` + db_number
     - For each key in `db`: write optional `0xFC` + expiry_ms if has expiry,
       write type byte, encode key as RDB string, encode value based on its type
   - Write `0xFF` end marker
   - Write CRC64 of everything written so far

2. Write `loadRDB(const string& filename)`:
   - Read and verify magic
   - Loop: read type byte, dispatch to type decoder, insert into `db`

3. Add `SAVE` command (blocking — freezes the server) and `BGSAVE` (fork-based, V8.1).

🔧 **String encoding in RDB**:
```
If value fits in 1 byte: 0b11000000 | (value & 0x3F)  (RDB_ENC_INT8)
If fits in 2 bytes:      0b11000001 + 16-bit LE
If fits in 4 bytes:      0b11000010 + 32-bit LE
Else:                    length prefix (1-5 bytes encoding the string length) + string bytes
```

🧪 **Test**:
```bash
redis-cli -p 8080 set foo bar
redis-cli -p 8080 set count 42
redis-cli -p 8080 hset user name Alice
redis-cli -p 8080 save          # writes dump.rdb

# Kill and restart your server (load RDB on startup)
redis-cli -p 8080 get foo       # bar (survived restart)
redis-cli -p 8080 get count     # 42
redis-cli -p 8080 hget user name  # Alice
```

---

### V8.1 — Fork-based BGSAVE

🎯 **Goal**: Non-blocking snapshot using `fork()` + copy-on-write so the server keeps serving
requests while the save happens in the background.

🧠 **Concept**: `fork()` on Linux creates a child process that gets an exact copy of the parent's
memory, but via the OS's copy-on-write (COW) mechanism — pages are shared until either process
writes to them, at which point only the writing process gets a private copy. So the child sees
a perfectly consistent snapshot of the dataset at the moment of `fork()`, while the parent
continues handling writes (which cause COW copies of modified pages, but never corrupt the
child's view).

```
parent: fork() → child_pid, continue serving clients
child:  sees frozen memory snapshot, writes dump.rdb, exits 0
parent: on SIGCHLD: reap child, note save complete, check exit code
```

🔧 **Implementation**:
```cpp
pid_t pid = fork();
if (pid == 0) {
    // child process
    saveRDB("dump.rdb");  // writes the snapshot
    _exit(0);             // _exit, not exit (don't flush parent's buffers)
}
// parent continues normally
// set a flag: bgsave_in_progress = true, bgsave_child_pid = pid
// handle SIGCHLD to know when child finishes
```

Signal handling: install `signal(SIGCHLD, sigchld_handler)`. In the handler, `waitpid(-1, &status, WNOHANG)`
to reap the child non-blocking.

⚠️ **IMPORTANT**: `fork()` on systems with overcommit disabled may fail if RSS is near memory
limit. Real Redis issues a warning about this. On Linux, `/proc/sys/vm/overcommit_memory` should
be 1 for production Redis.

---

### V8.2 — AOF (Append-Only File)

🎯 **Goal**: Log every write command to a file so the dataset can be reconstructed on restart
without depending on periodic snapshots.

🧠 **Concept**: The AOF file is just RESP-formatted commands written one after another:
```
*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
*4\r\n$4\r\nHSET\r\n$6\r\nuser:1\r\n$4\r\nname\r\n$5\r\nAlice\r\n
```
On restart, feed this file back through your command dispatcher — commands replay in order,
reconstructing the dataset exactly. This works because RESP is your native format already.

**fsync policies** (V8.3): the question is how often to flush kernel buffers to the disk:
- `always`: `fsync()` after every write. Slowest, safest. At most 1 command lost on crash.
- `everysec`: fsync once per second via timer. At most ~1 second of writes lost. Default.
- `no`: never fsync, let OS decide. Fastest. Could lose up to several seconds on crash.

📁 **Files**: Create `aof.h` / `aof.cpp`

🔧 **Implementation steps**:
1. On every write command (any command with the "write" flag in V6.0's command table):
   serialize it to RESP format and append to `aof_buf` (an in-memory buffer).
2. On each event loop iteration: flush `aof_buf` to the AOF file (`fwrite()`).
3. Implement fsync policy: a timer checks every second, calls `fsync(aof_fd)` if policy is `everysec`.
4. On startup: if AOF file exists, open it, read commands one by one using your RESP parser,
   dispatch each through the normal command path (with AOF writes disabled during this replay).
5. Add `BGREWRITEAOF` command: fork, iterate `db`, write a compact AOF (one command per key's
   current state), replace the old AOF file atomically via rename.

🧪 **Test**:
```bash
redis-cli -p 8080 set session:1 active
redis-cli -p 8080 incr pageviews
redis-cli -p 8080 hset user:1 email test@test.com
# Kill server (Ctrl+C)
cat appendonly.aof   # should see RESP commands for the above operations
# Restart server
redis-cli -p 8080 get session:1   # active (replayed from AOF)
redis-cli -p 8080 get pageviews   # 1
```

---

## Phase 9 — Pub/Sub

### V9.0 — Channel Subscriptions + PUBLISH/SUBSCRIBE

🎯 **Goal**: Implement the channel-based messaging system where clients subscribe to named channels
and publishers broadcast messages to all subscribers.

🧠 **Concept**: Pub/Sub is orthogonal to the key-value store. A subscribing client enters a special
state: it can only send SUBSCRIBE/UNSUBSCRIBE/PING, and it continuously receives pushed messages
(RESP3 push messages, or RESP2 array replies in the format `[message, channel, payload]`).

Data structure needed:
```cpp
unordered_map> channel_to_clients; // channel → set of subscriber fds
unordered_map> client_to_channels; // fd → set of subscribed channels
```

📁 **Files**: Create `pubsub.h` / `pubsub.cpp`

🔧 **Commands to implement**:

|
 Command 
|
 Behavior 
|
|
---
|
---
|
|
`SUBSCRIBE channel [channel ...]`
|
 Subscribe to channels. Enters pub/sub mode. For each channel: send 
`["subscribe", channel, total_count]`
|
|
`UNSUBSCRIBE [channel ...]`
|
 Remove subscription(s). If no channels left, exit pub/sub mode 
|
|
`PUBLISH channel message`
|
 Send message to all subscribers. Returns subscriber count 
|
|
`PSUBSCRIBE pattern [pattern ...]`
|
 Subscribe to channels matching a glob pattern 
|
|
`PUNSUBSCRIBE [pattern ...]`
|
 Remove pattern subscriptions 
|
|
`PUBSUB CHANNELS [pattern]`
|
 List active channels 
|
|
`PUBSUB NUMSUB [channel ...]`
|
 Count subscribers per channel 
|

🔧 **Implementation notes**:
- A client in pub/sub mode must have `is_subscriber = true` flag. Reject all non-pub/sub commands.
- PUBLISH: for each subscriber fd in `channel_to_clients[channel]`, format a push reply and add to
  that client's `write_buf`. The event loop's EPOLLOUT handling will flush it.
- When a client disconnects, call `cleanupSubscriptions(fd)` to remove it from all channel maps.

🧪 **Test** (two terminals):
```bash
# Terminal 1:
redis-cli -p 8080 subscribe news
# (now in subscribe mode, waiting for messages)

# Terminal 2:
redis-cli -p 8080 publish news "breaking: v9 implemented"
# Terminal 1 should print the message immediately
```

---

## Phase 10 — Transactions

### V10.0 — MULTI / EXEC / DISCARD

🎯 **Goal**: Implement Redis's transaction queuing — commands issued after MULTI are queued,
then executed atomically as a batch on EXEC.

🧠 **Concept**: Redis transactions provide atomicity (no other client's commands interleave
during EXEC) but NOT isolation (other clients can read dirty data between commands of different
transactions) and NOT rollback (if a queued command fails at runtime, the others still execute).
Atomicity is trivially guaranteed by the single-threaded event loop — EXEC just runs the whole
queue back-to-back without yielding.

Per-client state additions:
```cpp
bool in_multi = false;
bool multi_error = false;  // set if a command in queue had a compile-time error
vector<vector> queued_commands;  // argv vectors of queued commands
```

🔧 **Commands to implement**:
- `MULTI`: set `in_multi = true`, reply `+OK`
- Any command while `in_multi = true` and not EXEC/DISCARD/MULTI: push to `queued_commands`, reply `+QUEUED`
- `EXEC`: if `multi_error`, reply error, clear state; else run all queued commands sequentially and collect
  their replies into one array reply
- `DISCARD`: clear `queued_commands`, `in_multi = false`, reply `+OK`
- Invalid command while in MULTI: set `multi_error = true`, reply `-ERR ...` (command is still queued
  positionally but EXEC will fail the whole transaction — real Redis behavior since 2.6)

---

### V10.1 — WATCH (Optimistic Locking)

🎯 **Goal**: Let clients detect concurrent modification of a key between WATCH and EXEC,
aborting the transaction if the key was touched.

🧠 **Concept**: WATCH implements compare-and-swap at the key level without real locks:
1. Client WATCHes one or more keys
2. Client does some reads, builds a transaction with MULTI/EXEC
3. If any watched key was modified by ANY other client between WATCH and EXEC, EXEC returns nil
   (the transaction is aborted, not executed)
4. Client checks the nil reply and retries if needed

This is optimistic concurrency: assume no conflict, detect and retry if there was one.

Per-server: `unordered_map<string, unordered_set<int>> watched_keys` (key → set of watching client fds).
Per-client: `unordered_set<string> watches`, `bool dirty = false` (true = a watched key changed).

🔧 **Implementation**:
1. `WATCH key [key ...]`: add key→client to `watched_keys`, add key to `client.watches`
2. On every write command that modifies a key: for each watcher of that key, set `client.dirty = true`
3. `EXEC` check: if `client.dirty`, clear queue and watches, return nil array (`*-1\r\n`)
4. On client disconnect: clean up all watches for that fd

🧪 **Test**:
```bash
# Terminal 1 (simulate CAS update):
redis-cli -p 8080 watch mykey
redis-cli -p 8080 multi
redis-cli -p 8080 set mykey "new_value"
# Before exec in terminal 2:
# Terminal 2: redis-cli -p 8080 set mykey "interference"
redis-cli -p 8080 exec     # nil (aborted because terminal 2 modified it)

# Without interference: exec succeeds normally
```

---

## Phase 11 — Advanced Features (Optional, Resume-Level)

### V11.0 — Pipelining

🎯 **Goal**: Handle multiple commands sent in one network write without a reply between each.

🧠 **Concept**: Pipelining is already implicit in your RESP parser. A client can send
10 commands back-to-back in one `send()` call. Your parser's `while(parser.tryParse(argv))` loop
in the event handler already processes all of them. The key is confirming you don't do
`send()` to the client between each command mid-loop — instead accumulate all replies in
`write_buf` and flush once at the end of the read handling.

🔧 **Verify**: Your V2.0 event loop already does this correctly if you write to `client.write_buf`
inside the `tryParse` loop. Confirm with:
```bash
redis-benchmark -p 8080 -t set,get -n 100000 -P 16  # pipeline 16 commands per round-trip
# compare ops/sec with and without -P flag — should be dramatically higher with pipelining
```

---

### V11.1 — Lua Scripting (EVAL)

🎯 **Goal**: Execute server-side Lua scripts atomically.

🧠 **Concept**: Redis embeds a Lua 5.1 interpreter. An `EVAL` script runs as one atomic unit
(same as the whole event loop running it without yielding) — it can read and write to Redis by
calling `redis.call('SET', 'key', 'value')` which dispatches through the normal command path.

This solves the "I need to read, check, then conditionally write without a race" problem that
MULTI/EXEC with WATCH addresses in a clunkier way.

📁 **Files**: Create `scripting.cpp`, add Lua C library (link with `-llua5.1`)

🔧 **Implementation**:
1. Initialize a `lua_State*` once at server startup
2. Register a global `redis` table with a `call(cmd, ...)` function
3. `redis.call` implementation: take Lua string args, convert to `vector<string>`, dispatch
   through your command table, convert the result back to Lua type (string→string, integer→number,
   array→table, nil→false)
4. `EVAL script numkeys key [key ...] arg [arg ...]`: run `luaL_dostring(L, script)`,
   populate `KEYS` and `ARGV` tables in Lua before execution
5. `EVALSHA sha numkeys ...`: execute a cached script by its SHA1 hash (store scripts in
   `unordered_map<string, string> script_cache`)
6. `SCRIPT LOAD script` → returns SHA1, `SCRIPT EXISTS sha [sha ...]`, `SCRIPT FLUSH`

🧪 **Test** — classic atomic lock release:
```lua
-- Lua: only delete lock if it's yours
redis-cli -p 8080 eval "if redis.call('get', KEYS[1]) == ARGV[1] then return redis.call('del', KEYS[1]) else return 0 end" 1 lock:order:42 my_token
```

---

### V11.2 — Primary-Replica Replication

🎯 **Goal**: A replica connects to the primary, gets a full copy of the dataset, then receives
a real-time stream of write commands.

🧠 **Concept**: Redis replication protocol:
1. Replica sends `REPLCONF listening-port <port>` then `PSYNC repl_id repl_offset`
2. Primary: if it recognizes the replication ID and the replica's offset is within the
   backlog, do a **partial resync** (send just the backlog diff). Otherwise, do a **full resync**:
   - Fork + BGSAVE to generate an RDB
   - Send `FULLRESYNC <repl_id> <offset>` then send the RDB over the socket as raw bytes
   - Then start streaming every write command in RESP format (the replication stream)
3. Replica: receive and load the RDB, then apply each streamed command to its own dataset

**Replication backlog**: a fixed-size circular buffer (ring buffer) of recent write commands
on the primary. If the replica disconnects briefly and reconnects, and the backlog still
contains what it missed, a partial resync avoids a full RDB transfer.

📁 **Files**: Create `replication.h` / `replication.cpp`

🔧 **Key design decisions**:
- Replicas are read-only by default (`replica-read-only yes` in config). Write commands return
  `READONLY You can't write against a read only replica`.
- `INFO replication` command: returns primary/replica status, replica list, offsets.
- Primary tracks each replica's `fd`, `acknowledged_offset` (confirmed received), and flushes
  accumulated write commands to each replica's connection.
- Commands not to replicate: read-only commands, `DEBUG`, `CLIENT SETNAME`, etc. Use the `write`
  flag in your command table from V6.0.

---

### V11.3 — Cluster (Hash Slot Sharding) — Capstone

🎯 **Goal**: Shard the keyspace across multiple server instances using Redis Cluster's hash slot
scheme. Each node handles 1/N of 16,384 slots.

🧠 **Concept**:
- Every key maps to a slot: `slot = CRC16(key) % 16384`
- For `{tag}` in a key: `CRC16(tag)` instead of the full key (hash tags for co-location)
- Each node knows which slots it owns and where other slots live (slot map)
- `MOVED slot ip:port` redirect: if a client sends a command for a key not owned by this node,
  reply with a MOVED redirect pointing to the correct node (do NOT proxy it transparently —
  clients are responsible for following redirects and caching the slot map)
- `ASK slot ip:port`: temporary redirect during slot migration (client should follow once but
  not update its slot map cache)
- Nodes gossip via a separate cluster bus port (client_port + 10000), exchanging node state,
  slot ownership, and failure reports

📁 **Files**: Create `cluster.h` / `cluster.cpp`

🔧 **Key components**:
1. Slot ownership table: `uint16_t slot_owner[16384]` — index maps to a node ID or -1 (myself)
2. Node registry: `map<uint16_t, ClusterNode>` (node ID → ip, port, slot ranges, health)
3. `CLUSTER NODES`, `CLUSTER INFO`, `CLUSTER MYID`, `CLUSTER MEET ip port`
4. Slot migration: `CLUSTER SETSLOT slot MIGRATING node_id` (mark as migrating),
   `CLUSTER SETSLOT slot IMPORTING node_id` (mark as importing),
   `MIGRATE host port key dbid timeout` (move a key to another node)
5. Gossip: periodic `PING`/`PONG`/`MEET` messages between nodes on the bus port, carrying
   node state and partial cluster views

---

## Phase 12 — Benchmarking, Observability & Resume Packaging

### V12.0 — INFO Command + CONFIG GET/SET

Add the `INFO` command with sections:
- `INFO server`: version, OS, uptime
- `INFO clients`: connected_clients, blocked_clients
- `INFO memory`: used_memory, mem_fragmentation_ratio
- `INFO stats`: total_commands_processed, total_connections_received, ops_per_sec
- `INFO replication`: role, connected_slaves, master_replid
- `INFO keyspace`: db0:keys=N,expires=M

Add `CONFIG GET parameter` and `CONFIG SET parameter value` to read/write live config.

### V12.1 — Benchmarking

Run `redis-benchmark` against your server and compare:
```bash
# Baseline (no pipeline):
redis-benchmark -p 8080 -t set,get,incr,lpush,rpush,lrange -n 100000 -q

# With pipelining:
redis-benchmark -p 8080 -t set,get -n 100000 -P 16 -q

# With multiple clients:
redis-benchmark -p 8080 -t set,get -n 100000 -c 50 -q
```

Compare your ops/sec to real Redis. Track where the gap is. Common bottlenecks:
- Memory allocation (are you calling `new`/`malloc` too often on the hot path?)
- String copies (are you copying `argv` vectors when you could pass by const ref?)
- Write buffer implementation (is appending to `write_buf` causing reallocations?)

### V12.2 — Resume Packaging

The three artifacts that make this project interview-proof:

**1. Architecture diagram** (draw.io / Excalidraw):
```
[Client] ──RESP2──> [TCP Listener]
                         │
                    [epoll event loop]
                         │
              ┌──────────┴──────────┐
         [RESP Parser]        [Timer Events]
              │                    │
         [Command Table]    [Active Expiry] [BGSAVE trigger]
              │
   ┌──────────┼────────────┐
[String]  [Hash]  [List]  [Set]  [ZSet]
[SDS]   [listpack/  [quicklist]  [intset/  [skiplist+
         hashtable]              listpack/  hashtable]
                                 hashtable]
              │
      ┌───────┴────────┐
   [RDB]            [AOF]
   (fork+COW)   (RESP log + fsync)
```

**2. Design document** (1–2 pages):
Write a short doc answering:
- Why single-threaded event loop instead of a thread pool?
- Why skip list over red-black tree for sorted sets?
- How does fork()+COW make non-blocking snapshotting possible?
- What's the trade-off between RDB and AOF? Why run both?
- What consistency guarantees does this server provide?
- What's one thing you'd change if you rebuilt it?

**3. Benchmarks section in README**:
|
 Scenario 
|
 Your server 
|
 Real Redis 
|
 Gap 
|
|
---
|
---
|
---
|
---
|
|
 GET/SET (no pipeline) 
|
 X k ops/sec 
|
 100-120k 
|
 ... 
|
|
 GET/SET (pipeline=16) 
|
 X k ops/sec 
|
 800k+ 
|
 ... 
|
|
 ZADD leaderboard 
|
 X k ops/sec 
|
 ... 
|
 ... 
|

Own the gap — explain it (no fine-tuned buffer sizes, no jemalloc, no sendfile) rather than hiding it.

---

## Quick Reference — Version Checklist

```
[x] V0.1  SO_REUSEADDR, per-client recv buffer with line framing
[x] V0.2  Proper tokenizer, vector<string> argv, arity checks
[x] V1.0  RESP2 decoder (state machine, feed/tryParse pattern)
[x] V1.1  RESP2 encoder (encodeOK, encodeBulkString, encodeArray...)
[ ] V2.0  poll()-based event loop, per-client struct, non-blocking sockets
[x] V2.1  epoll-based loop, EPOLLOUT only when needed
[x] V3.0  RedisObject type wrapper (type + encoding + void* ptr)
[x] V3.1  Full string commands (INCR, APPEND, MSET/MGET, EX/NX options)
[x] V3.2  Hash commands (HSET, HGET, HGETALL, HINCRBY, WRONGTYPE check)
[x] V3.3  List commands (LPUSH/RPUSH, LPOP/RPOP, LRANGE, LINDEX)
[x] V3.4  Set commands (SADD/SREM, SMEMBERS, SINTER/SUNION/SDIFF)
[ ] V3.5  Sorted Set commands (ZADD, ZRANGE, ZRANK, skip list implementation)
[x] V4.0  Expiry metadata map
[x] V4.1  Lazy expiry on every read
[x] V4.2  Active expiry sweep (periodic, sampling-based)
[x] V4.3  EXPIRE/TTL/PERSIST command family
[x] V5.0  SDS strings
[ ] V5.1  listpack encoding for small collections
[ ] V5.2  intset encoding for integer sets
[ ] V5.3  Auto-promotion between encodings (OBJECT ENCODING shows the result)
[ ] V6.0  Command dispatch table, INFO/CONFIG/KEYS/SCAN/DEBUG commands
[ ] V7.0  maxmemory config + approximated LRU/LFU eviction
[ ] V8.0  RDB snapshot (blocking SAVE)
[ ] V8.1  Fork-based BGSAVE (non-blocking)
[ ] V8.2  AOF append + replay on startup
[ ] V8.3  AOF fsync policies (always/everysec/no) + BGREWRITEAOF
[ ] V9.0  Pub/Sub (SUBSCRIBE, PUBLISH, PSUBSCRIBE, channel state machine)
[ ] V10.0 MULTI/EXEC/DISCARD transactions
[ ] V10.1 WATCH optimistic locking
[ ] V11.0 Pipelining verification + benchmark
[ ] V11.1 Lua scripting (EVAL/EVALSHA, redis.call())
[ ] V11.2 Primary-Replica replication (PSYNC, RDB transfer, write stream)
[ ] V11.3 Cluster (hash slots, MOVED/ASK redirects, gossip) ← optional capstone
[ ] V12   INFO/CONFIG commands, benchmarks, architecture diagram, design doc
```

---

## Key References

- **Real Redis source** (read alongside each version): https://github.com/redis/redis  
  → `src/ae.c` (event loop), `src/sds.c`, `src/dict.c`, `src/t_zset.c`, `src/listpack.c`
- **Build Your Own Redis** (free book, C/C++): https://build-your-own.org/redis/
- **RESP2 spec**: https://redis.io/docs/latest/develop/reference/protocol-spec/
- **Redis persistence docs** (RDB/AOF internals): https://redis.io/docs/latest/operate/oss_and_stack/management/persistence/
- **Redis cluster spec** (authoritative hash slot / gossip documentation): https://redis.io/docs/latest/operate/oss_and_stack/reference/cluster-spec/
- **Arpit Bhayani's Redis Internals** (free first videos, great for event loop + RESP): https://arpitbhayani.me/redis-internals/
- **CodeCrafters Redis challenge** (guided, multi-language): https://codecrafters.io

---

> **Reminder on working with Claude**: Bring Claude a design decision, not "write me the skip list."
> "I'm implementing the skip list — should the span be tracked per node at each level or in a
> separate array? What are the trade-offs?" gets you 10x more useful output than "implement ZSET."
> The code you write yourself, even slowly, is the code you can explain in an interview.
