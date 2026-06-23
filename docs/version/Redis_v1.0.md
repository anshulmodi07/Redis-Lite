# Redis_v1.0 Documentation

# Goal and Motivation

V1.0 adds a RESP2 decoder so clients can send Redis-style command frames to the server.

The important internal shape from V0.2 is preserved: command execution still receives `vector<string> argv`. This keeps the protocol parser separate from command dispatch and prepares the code for RESP responses in V1.1.

# Previous Limitation Being Fixed

V0.2 used newline-delimited plain text with a custom tokenizer. That was useful for learning, but real Redis clients send arrays of bulk strings:

```text
*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
```

Without a RESP decoder, `redis-cli` and Redis client libraries cannot speak to the server.

# Concepts Taught

- RESP2 array and bulk string framing.
- Parser-owned receive buffering.
- `feed()` / `tryParse()` parser flow for partial TCP reads.
- Pipelining at the parser level: multiple complete commands can be extracted from one receive buffer.
- Deferring buffer consumption until a complete frame is available.
- Protocol errors for malformed frames.

# Design Decisions and Trade-Offs

- `RespParser` owns raw bytes received from one client. Each client connection gets its own parser instance.
- `tryParse()` returns one command at a time and leaves incomplete frames buffered.
- The parser consumes bytes only after a full command has been validated.
- RESP command input must be an array of bulk strings. Null arrays and null bulk strings are rejected for command input.
- The V0.2 inline text path is kept as a compatibility fallback. This preserves the existing manual client and regression tests, and it matches Redis's useful inline command form.
- Responses are still plain text in V1.0. RESP response encoding is intentionally left for V1.1.

# Files Added or Changed

- `resp.h`
  - Declares the `RespParser` class.
- `resp.cpp`
  - Implements parser buffering, RESP array parsing, bulk string parsing, inline fallback parsing, and malformed-frame errors.
- `server.cpp`
  - Replaces the per-client line buffer with a per-client `RespParser`.
  - Feeds raw `recv()` bytes into the parser.
  - Dispatches every complete parsed command in a loop.
- `parser.cpp`
  - Normalizes command names inside `dispatch()` so RESP input and tokenizer input behave consistently.
- `tests/test_v0_1.py` and `tests/test_v0_2.py`
  - Compile the new `resp.cpp` module with the server.
- `tests/test_v1_0.py`
  - Adds focused RESP decoder regression coverage.
- `.gitignore`
  - Ignores the generated V1.0 test binary.
- `docs/structure.md`
  - Updates the project map for the RESP parser module, test, and version document.

# Behavior and Commands Added

The command set is unchanged:

```text
PING
SET key value
GET key
```

New accepted input format:

```text
*1\r\n$4\r\nPING\r\n
*3\r\n$3\r\nSET\r\n$5\r\nspace\r\n$11\r\nhello world\r\n
*2\r\n$3\r\nGET\r\n$5\r\nspace\r\n
```

Multiple RESP commands can arrive in one TCP write and are processed in order.

Malformed input example:

```text
*1\r\n+PING\r\n -> ERR protocol error: expected bulk string
```

# Testing Steps and Results

Regression and V1.0 tests:

```bash
python3 tests/test_v0_1.py
python3 tests/test_v0_2.py
python3 tests/test_v1_0.py
```

Result:

```text
v0.1 socket tests passed
v0.2 tokenizer tests passed
v1.0 resp decoder tests passed
```

They were run in a disposable Linux Docker container because the local Windows MinGW compiler does not provide POSIX socket headers.

# Known Limitations

- Responses are not RESP-encoded yet, so `redis-cli` can send commands but will not display replies correctly until V1.1.
- The server still uses one detached thread per client.
- There is no event loop or non-blocking socket mode yet.
- Only RESP arrays of bulk strings are supported for structured command frames.
- Data remains in memory only and is lost on restart.

# What the Next Version Builds Upon

V1.1 can replace plain-text command responses with RESP2 encoders such as simple strings, errors, bulk strings, null bulk strings, integers, and arrays. After that, the server will speak RESP in both directions.
