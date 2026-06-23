# Redis_v1.1 Documentation

# Goal and Motivation

V1.1 replaces plain-text server replies with RESP2-encoded responses.

After V1.0, clients could send RESP commands, but responses were still raw text such as `OK\n` and `NOT FOUND\n`. V1.1 makes replies protocol-correct so Redis clients can parse success, errors, values, and missing keys.

# Previous Limitation Being Fixed

The server had a half-RESP protocol: RESP input, plain-text output. That breaks real Redis clients because they expect every reply to start with a RESP type prefix such as `+`, `-`, `$`, `:`, or `*`.

# Concepts Taught

- RESP simple strings for success replies.
- RESP errors for command and protocol failures.
- RESP bulk strings for arbitrary stored values.
- RESP null bulk strings for missing keys.
- Keeping protocol formatting in helper functions instead of scattering string concatenation through command handlers.

# Design Decisions and Trade-Offs

- `PING` returns `+PONG\r\n`.
- `SET` returns `+OK\r\n`.
- `GET key` returns a bulk string, so values can contain spaces or newlines safely.
- Missing keys return `$-1\r\n` instead of the old `NOT FOUND\n`.
- Errors return `-ERR ...\r\n`.
- Encoder helpers live in `resp.h` / `resp.cpp` alongside the decoder because RESP wire formatting belongs to the protocol layer.
- Inline text commands are still accepted, but their replies are RESP encoded. This intentionally changes the old manual-client output.

# Files Added or Changed

- `resp.h`
  - Adds declarations for RESP response encoder helpers.
- `resp.cpp`
  - Adds `encodeSimpleString`, `encodeOK`, `encodeError`, `encodeInteger`, `encodeBulkString`, `encodeNullBulk`, `encodeArray`, and `encodeNullArray`.
- `parser.cpp`
  - Updates `PING`, `SET`, `GET`, wrong arity, unknown command, and missing key replies to use RESP encoders.
- `server.cpp`
  - Encodes request-size and protocol/parser errors as RESP errors.
- `tests/test_v0_1.py`, `tests/test_v0_2.py`, and `tests/test_v1_0.py`
  - Update expected replies from plain text to RESP frames.
- `tests/test_v1_1.py`
  - Adds focused coverage for RESP-encoded replies.
- `.gitignore`
  - Ignores the generated V1.1 test binary.
- `docs/structure.md`
  - Updates the project map for V1.1.

# Behavior and Commands Added

The command set is unchanged:

```text
PING
SET key value
GET key
```

Response mapping:

```text
PONG                -> +PONG\r\n
OK                  -> +OK\r\n
ERR message         -> -ERR message\r\n
value               -> $N\r\nvalue\r\n
missing key         -> $-1\r\n
```

Examples:

```text
PING                -> +PONG\r\n
SET k v             -> +OK\r\n
GET k               -> $1\r\nv\r\n
GET missing         -> $-1\r\n
BADCMD              -> -ERR unknown command\r\n
```

# Testing Steps and Results

Regression and V1.1 tests:

```bash
python3 tests/test_v0_1.py
python3 tests/test_v0_2.py
python3 tests/test_v1_0.py
python3 tests/test_v1_1.py
```

Result:

```text
v0.1 socket tests passed
v0.2 tokenizer tests passed
v1.0 resp decoder tests passed
v1.1 resp encoder tests passed
```

They were run in a disposable Linux Docker container because the local Windows MinGW compiler does not provide POSIX socket headers.

# Known Limitations

- Only `PING`, `SET`, and `GET` are implemented.
- The server still uses one detached thread per client.
- There is no event loop or non-blocking socket mode yet.
- The response helpers support arrays and integers, but current commands do not return those types yet.
- Data remains in memory only and is lost on restart.

# What the Next Version Builds Upon

V2.0 can now move the networking model to a poll-based event loop without also changing command or protocol semantics. The server speaks RESP in both directions, so future commands can focus on behavior rather than wire-format plumbing.
