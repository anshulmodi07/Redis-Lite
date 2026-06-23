# Redis_v2.1 Documentation

# Goal and Motivation

V2.1 replaces the V2.0 `poll()` reactor with Linux `epoll`.

The command set and RESP behavior stay unchanged. The improvement is in I/O multiplexing: instead of rebuilding and scanning a full `pollfd` array on every loop, the server keeps interest state in the kernel and receives only ready file descriptors from `epoll_wait()`.

# Previous Limitation Being Fixed

V2.0 used `poll()`, which scans every registered descriptor even when only one client is ready. That is fine for learning, but it becomes wasteful as client count grows. `epoll` is the next step toward Redis-style Linux networking.

# Concepts Taught

- `epoll_create1()` for an event-loop instance.
- `epoll_ctl(ADD/MOD/DEL)` for fd interest management.
- `epoll_wait()` for ready-event delivery.
- Level-triggered epoll as the simpler first implementation.
- Registering `EPOLLOUT` only while a client has pending response bytes.
- Removing client fds from epoll before closing them.

# Design Decisions and Trade-Offs

- The event loop uses level-triggered epoll, matching the guide's recommended learning path.
- `EPOLLOUT` is toggled through `EPOLL_CTL_MOD` after each read/write pass based on `Client::write_buf`.
- The listening socket is registered once for `EPOLLIN`.
- Accepted clients are non-blocking and registered with `EPOLLIN | EPOLLERR | EPOLLHUP`.
- Per-client parser and write-buffer state from V2.0 is preserved.
- `MAX_EVENTS` is fixed at 64 for now; later benchmarking can tune this.
- This is Linux-only because `epoll` is a Linux API.

# Files Added or Changed

- `eventloop.cpp`
  - Replaces `poll()` and `pollfd` rebuilding with `epoll_create1`, `epoll_ctl`, and `epoll_wait`.
  - Adds helpers for epoll registration, client interest updates, and epoll-aware close.
  - Keeps non-blocking accept/read/write behavior from V2.0.
- `tests/test_v2_1.py`
  - Adds epoll-focused regression coverage for idle clients and pipelined reply ordering.
- `.gitignore`
  - Ignores the generated V2.1 test binary.
- `docs/structure.md`
  - Updates the current project map from V2.0 to V2.1.
- `docs/version/Redis_v2.1.md`
  - Documents this version boundary.

# Behavior and Commands Added

No new commands were added.

Existing commands remain:

```text
PING
SET key value
GET key
```

The networking behavior remains:

```text
idle client held open       -> active clients still receive responses
pipelined commands          -> replies are buffered and returned in order
client with pending writes  -> EPOLLOUT registered until write_buf is empty
client with no pending data -> EPOLLOUT removed to avoid busy wakeups
```

# Testing Steps and Results

Intended regression suite:

```bash
python3 tests/test_v0_1.py
python3 tests/test_v0_2.py
python3 tests/test_v1_0.py
python3 tests/test_v1_1.py
python3 tests/test_v2_0.py
python3 tests/test_v2_1.py
```

Suggested benchmark from the guide:

```bash
redis-benchmark -p 8080 -t set,get -n 100000 -q
```

WSL verification completed successfully:

```text
v2.1 epoll event loop tests passed
```

Windows/MinGW still cannot compile this version because it lacks the POSIX/Linux networking headers used by `server.cpp` and `eventloop.cpp`.

# Known Limitations

- Only `PING`, `SET`, and `GET` are implemented.
- The server is Linux-only after this version because it uses `epoll`.
- There is no TTL, persistence, typed value system, command table, or eviction.
- Data remains in memory only and is lost on restart.
- Tests and benchmarks require a Linux environment with `g++`, Python 3, and socket support.

# What the Next Version Builds Upon

V3.0 can now start the type-system phase on top of a single-threaded, Linux-style event loop with per-client parser and output-buffer state already in place.
