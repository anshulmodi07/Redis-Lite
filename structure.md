# Project Structure

Current version: V0.1

```text
.
|-- client.cpp          # One-command TCP client for manual checks
|-- server.cpp          # Thread-per-client TCP key-value server
|-- tests/
|   `-- test_v0_1.py    # V0.1 socket framing and restart regression tests
|-- guide.md            # Version roadmap and implementation guide
|-- rules.md            # Project workflow and completion rules
|-- Redis_v0.md         # Original V0 documentation
`-- Redis_v0.1.md       # V0.1 documentation
```

## File Responsibilities

- `server.cpp` owns the listening socket, accept loop, per-client recv buffers, command execution, response writes, and the in-memory string database.
- `client.cpp` connects to `127.0.0.1:8080`, sends one newline-delimited command from stdin, prints one response, and exits.
- `tests/test_v0_1.py` compiles the server and verifies the V0.1 socket hygiene behavior through real TCP connections.
- `guide.md` remains the roadmap source of truth for future versions.
- `Redis_v*.md` files document completed version boundaries.
