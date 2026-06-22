# Redis_v0.1 Documentation

# Goal and Motivation

V0.1 hardens the original TCP key-value server before new Redis features are added.

The version fixes restart friction, partial TCP reads, unsafe shared state lifetime, and silent unknown commands while preserving the plain-text `SET` and `GET` behavior from V0.

# Previous Limitation Being Fixed

V0 treated each `recv()` call as one complete command, but TCP is a byte stream and may split or combine application messages. It also did not set `SO_REUSEADDR`, so quick restarts could fail with `Address already in use`. Detached client threads used references to database state stored on `main()`'s stack.

# Concepts Taught

- `SO_REUSEADDR` before `bind()` for smoother development restarts.
- Per-client line buffering for newline-delimited TCP commands.
- Partial read and partial write handling.
- File-scope shared state as a simple lifetime fix for detached threads.
- Defensive limits for unterminated client input.

# Design Decisions and Trade-Offs

- Commands are still plain text and newline-delimited. RESP replaces this in a later version.
- The database and mutex are file-scope globals to match the V0.1 scope without introducing a larger server abstraction early.
- Responses now end with `\n`, which makes client parsing deterministic.
- The existing client appends `\n` after user input so the manual one-command workflow continues to work.
- Commands currently accept single-token values only. Rich command tokenization is intentionally left for V0.2.

# Files Added or Changed

- `server.cpp`
  - Added `SO_REUSEADDR`.
  - Added per-client receive buffering and newline command framing.
  - Added reliable `sendAll()` response writes.
  - Moved `db` and `db_mutex` to file scope.
  - Added `PING` and explicit error responses.
- `client.cpp`
  - Appends a newline to the user command before sending.
- `tests/test_v0_1.py`
  - Adds real TCP regression tests for V0.1.
- `structure.md`
  - Adds the project structure map required by the rules.
- `guide.md`
  - Corrects the V0.1 socket option typo from `SO_REUSEOPT` to `SO_REUSEADDR`.
- `.gitignore`
  - Ignores the generated V0.1 test server binary.

# Behavior and Commands Added

Existing commands:

```text
SET key value -> OK
GET key       -> value or NOT FOUND
```

New command:

```text
PING -> PONG
```

Errors:

```text
ERR unknown command
ERR wrong number of arguments
ERR request too large
```

# Testing Steps and Results

The focused V0.1 test script:

```bash
python tests/test_v0_1.py
```

Result:

```text
v0.1 socket tests passed
```

The test was run in a disposable Linux Docker container because the local Windows MinGW compiler does not provide POSIX socket headers.

It verifies:

- Server compilation.
- Split TCP input, using `PI` followed by `NG\n`.
- Multiple newline-delimited commands in one connection.
- `SET`, `GET`, missing key, `PING`, and unknown command responses.
- Windows-style `\r\n` input.
- Bad arity handling.
- Disconnect behavior for oversized unterminated input.
- Immediate server restart after termination.

# Known Limitations

- The server still uses one detached thread per client.
- Shutdown is still rough and handled by process termination in tests.
- Values cannot contain spaces yet.
- The wire protocol is still temporary plain text, not RESP.
- Data is still in memory only and is lost on restart.

# What the Next Version Builds Upon

V0.2 can build a cleaner command tokenizer on top of the newline-framed input introduced here. That will make argument validation and quoted or multi-word values easier to reason about before RESP arrives.
