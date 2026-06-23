# Project Structure

Current version: V2.0

```text
.
|-- client.cpp          # One-command TCP client for manual checks
|-- client.h            # Per-connection event-loop client state
|-- eventloop.cpp       # poll()-based reactor, non-blocking client I/O, and shared string DB
|-- eventloop.h         # Event-loop entry point
|-- parser.cpp          # Plain-text tokenizer and command dispatch
|-- parser.h            # Parser and dispatch declarations
|-- resp.cpp            # RESP2 decoder, inline fallback, and response encoders
|-- resp.h              # RespParser and RESP encoder declarations
|-- server.cpp          # Listening socket setup and event-loop startup
|-- docs/
|   |-- guide.md        # Version roadmap and implementation guide
|   |-- rules.md        # Project workflow and completion rules
|   |-- structure.md    # Current project map
|   `-- version/
|       |-- Redis_v0.md
|       |-- Redis_v0.1.md
|       |-- Redis_v0.2.md
|       |-- Redis_v1.0.md
|       |-- Redis_v1.1.md
|       `-- Redis_v2.0.md
`-- tests/
    |-- test_v0_1.py    # V0.1 socket framing and restart regression tests
    |-- test_v0_2.py    # V0.2 tokenizer and dispatch regression tests
    |-- test_v1_0.py    # V1.0 RESP decoder regression tests
    |-- test_v1_1.py    # V1.1 RESP encoder regression tests
    `-- test_v2_0.py    # V2.0 poll event-loop regression tests
```

## File Responsibilities

- `server.cpp` owns the listening socket setup, `SO_REUSEADDR`, bind/listen, SIGPIPE handling, and event-loop startup.
- `client.h` defines the per-client state used by the reactor: socket fd, RESP parser, pending write buffer, and close-after-flush flag.
- `eventloop.h` / `eventloop.cpp` own the single-threaded `poll()` loop, non-blocking server/client sockets, accept draining, per-client read/write readiness handling, parser feeding, response buffering, and the in-memory string database.
- `parser.h` / `parser.cpp` own plain-text command tokenization, command-name normalization, arity validation, and dispatch for `PING`, `SET`, and `GET`.
- `resp.h` / `resp.cpp` own RESP2 array-of-bulk-string decoding, parser buffering, partial frame handling, inline command fallback, and response encoding helpers.
- `client.cpp` connects to `127.0.0.1:8080`, sends one newline-delimited command from stdin, prints one response, and exits.
- `tests/test_v0_1.py` compiles the server and verifies the V0.1 socket hygiene behavior through real TCP connections.
- `tests/test_v0_2.py` compiles the server and verifies quoted values, case-insensitive commands, escaped quotes, and arity errors.
- `tests/test_v1_0.py` compiles the server and verifies RESP arrays, partial bulk strings, pipelined commands, and malformed RESP errors.
- `tests/test_v1_1.py` compiles the server and verifies RESP simple strings, errors, bulk strings, and null bulk responses.
- `tests/test_v2_0.py` compiles the server and verifies an idle client does not block another client plus independent parser state across simultaneous clients.
- `docs/guide.md` remains the roadmap source of truth for future versions.
- `docs/rules.md` defines the version workflow and completion gate.
- `docs/version/Redis_v*.md` files document completed version boundaries.
