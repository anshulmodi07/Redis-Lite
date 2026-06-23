# Redis_v2.0 Documentation

# Goal and Motivation

V2.0 replaces the thread-per-client server with a single-threaded `poll()` event loop.

The server still speaks the same RESP2 protocol and supports the same commands, but connections are now managed by one reactor loop instead of detached OS threads.

# Previous Limitation Being Fixed

V1.1 created one detached thread for every client. That model is simple, but it scales poorly because each connection consumes thread stack memory and adds scheduler overhead. It also forced the string database to use a mutex even though command execution did not need true parallelism.

# Concepts Taught

- Reactor-style networking with `poll()`.
- Non-blocking listening and client sockets.
- Per-client state for parser and pending response bytes.
- Readiness-based writes through a response buffer.
- Accept-loop draining until `EAGAIN` / `EWOULDBLOCK`.
- Single-threaded command execution without database locking.

# Design Decisions and Trade-Offs

- `server.cpp` only creates, binds, listens, ignores SIGPIPE, and starts the event loop.
- `eventloop.cpp` owns client lifecycle, readiness handling, the shared in-memory string DB, and response flushing.
- `client.h` keeps per-connection state small and explicit.
- `dispatch()` no longer accepts a mutex because all commands run on the event-loop thread.
- Slow or blocked client writes are stored in `Client::write_buf` and flushed only when `poll()` reports `POLLOUT`.
- Protocol errors and oversized requests queue a RESP error and mark the client for close after the pending reply is flushed.
- `poll()` is O(N), which is acceptable for this learning version. V2.1 will replace this scaffold with `epoll`.

# Files Added or Changed

- `client.h`
  - Adds the `Client` struct with fd, `RespParser`, write buffer, and close-after-flush flag.
- `eventloop.h` / `eventloop.cpp`
  - Adds `runEventLoop()`, non-blocking socket setup, accept draining, `poll()` readiness handling, client read/write paths, and the shared string DB.
- `server.cpp`
  - Removes detached threads and delegates client handling to the event loop.
- `parser.h` / `parser.cpp`
  - Removes mutex plumbing from command dispatch.
- `tests/test_v0_1.py`, `tests/test_v0_2.py`, `tests/test_v1_0.py`, and `tests/test_v1_1.py`
  - Include `eventloop.cpp` in server compilation.
- `tests/test_v2_0.py`
  - Adds focused event-loop behavior coverage.
- `.gitignore`
  - Ignores the generated V2.0 test binary.
- `docs/structure.md`
  - Updates the project map for V2.0.

# Behavior and Commands Added

No new user-facing commands were added.

Existing commands remain:

```text
PING
SET key value
GET key
```

The behavioral change is connection handling:

```text
idle client held open       -> another client can still PING immediately
multiple connected clients  -> each keeps independent parser and output state
malformed request           -> RESP error is queued before the connection closes
oversized request           -> -ERR request too large, then close after flush
```

# Testing Steps and Results

Intended regression suite:

```bash
python3 tests/test_v0_1.py
python3 tests/test_v0_2.py
python3 tests/test_v1_0.py
python3 tests/test_v1_1.py
python3 tests/test_v2_0.py
```

Local Windows verification was attempted with the bundled Python runtime:

```text
C:\Users\91854\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe tests\test_v2_0.py
```

Result:

```text
server.cpp: fatal error: netinet/in.h: No such file or directory
eventloop.cpp: fatal error: poll.h: No such file or directory
```

This machine has MinGW `g++`, which does not provide the POSIX socket headers required by the project. WSL is installed but has no Linux distribution, and Docker Desktop is not running, so the Linux runtime tests could not be executed here.

# Known Limitations

- Only `PING`, `SET`, and `GET` are implemented.
- The event loop uses `poll()`, so each wakeup scans all connected clients.
- There is no TTL, persistence, typed value system, command table, or eviction.
- Data remains in memory only and is lost on restart.
- Tests require a Linux/POSIX environment with `g++`, Python 3, and socket support.

# What the Next Version Builds Upon

V2.1 can replace the `poll()` loop with an `epoll` loop while preserving the `Client` state, response buffering, parser integration, and single-threaded command execution model introduced here.
