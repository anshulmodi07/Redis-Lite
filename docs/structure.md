# Project Structure

Current version: V1.0

```text
.
|-- client.cpp          # One-command TCP client for manual checks
|-- parser.cpp          # Plain-text command tokenizer and command dispatch
|-- parser.h            # Parser and dispatch declarations
|-- resp.cpp            # RESP2 command decoder and inline command fallback
|-- resp.h              # RespParser declaration
|-- server.cpp          # Thread-per-client TCP key-value server
|-- tests/
|   |-- test_v0_1.py    # V0.1 socket framing and restart regression tests
|   |-- test_v0_2.py    # V0.2 tokenizer and dispatch regression tests
|   `-- test_v1_0.py    # V1.0 RESP decoder regression tests
|-- guide.md            # Version roadmap and implementation guide
|-- rules.md            # Project workflow and completion rules
`-- version/
    |-- Redis_v0.md     # Original V0 documentation
    |-- Redis_v0.1.md   # V0.1 documentation
    |-- Redis_v0.2.md   # V0.2 documentation
    `-- Redis_v1.0.md   # V1.0 documentation
```

## File Responsibilities

- `server.cpp` owns the listening socket, accept loop, per-client protocol parser instances, response writes, and the in-memory string database.
- `parser.h` / `parser.cpp` own plain-text command tokenization, command-name normalization, arity validation, and dispatch for `PING`, `SET`, and `GET`.
- `resp.h` / `resp.cpp` own RESP2 array-of-bulk-string decoding, parser buffering, partial frame handling, and inline command fallback.
- `client.cpp` connects to `127.0.0.1:8080`, sends one newline-delimited command from stdin, prints one response, and exits.
- `tests/test_v0_1.py` compiles the server and verifies the V0.1 socket hygiene behavior through real TCP connections.
- `tests/test_v0_2.py` compiles the server and verifies quoted values, case-insensitive commands, escaped quotes, and new arity errors.
- `tests/test_v1_0.py` compiles the server and verifies RESP arrays, partial bulk strings, pipelined commands, and malformed RESP errors.
- `guide.md` remains the roadmap source of truth for future versions.
- `docs/version/Redis_v*.md` files document completed version boundaries.
