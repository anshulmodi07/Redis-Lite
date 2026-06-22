# Redis_v0.2 Documentation

# Goal and Motivation

V0.2 replaces ad hoc `stringstream` command parsing with an argument-vector tokenizer and a dedicated dispatch function.

The server still uses the temporary newline-delimited text protocol from V0.1, but command execution now receives `vector<string> argv`, matching the representation that the RESP parser will produce in V1.

# Previous Limitation Being Fixed

V0.1 could only parse whitespace-separated commands with single-word values. A command like:

```text
SET mykey "hello world"
```

stored only a broken fragment of the intended value. The parsing logic also lived directly inside `server.cpp`, which would make the upcoming RESP input path harder to swap in cleanly.

# Concepts Taught

- Redis-style `argv` command representation.
- Case-insensitive command names through command normalization.
- Quoted token parsing for values that contain spaces.
- Escaped characters inside quoted strings.
- Command dispatch separated from socket read/write code.
- Per-command arity validation before accessing arguments.

# Design Decisions and Trade-Offs

- The tokenizer remains intentionally simple because RESP will replace this text protocol in V1.
- Only double-quoted tokens are special. Bare tokens still split on whitespace.
- A backslash inside quotes escapes the next character, which supports values such as `"hello \"redis\""`.
- Command names are uppercased during tokenization, so `get`, `Get`, and `GET` dispatch the same way.
- Wrong arity errors now include the command name, matching the guide's Redis-like shape.
- The database and mutex remain owned by `server.cpp`; dispatch receives them by reference to avoid introducing a larger server abstraction before it is useful.

# Files Added or Changed

- `parser.h`
  - Declares `tokenize()` and `dispatch()`.
- `parser.cpp`
  - Implements quoted-token parsing, command normalization, command dispatch, and command-specific handlers.
- `server.cpp`
  - Removes `stringstream` parsing and calls `dispatch(tokenize(line), db, db_mutex)`.
  - Converts tokenizer errors into `ERR ...` responses.
- `tests/test_v0_1.py`
  - Compiles `parser.cpp` with the server after the module split.
- `tests/test_v0_2.py`
  - Adds focused TCP regression tests for V0.2 parsing behavior.
- `.gitignore`
  - Ignores the generated V0.2 test binary.
- `docs/structure.md`
  - Updates the project map for the new parser module, test, and version document.

# Behavior and Commands Added

Existing commands are preserved:

```text
PING          -> PONG
SET key value -> OK
GET key       -> value or NOT FOUND
```

New parsing behavior:

```text
set mykey "hello world" -> OK
GET mykey               -> hello world
SET quote "hi \"you\""  -> OK
```

Updated errors:

```text
GET key extra          -> ERR wrong number of arguments for 'GET' command
SET broken "unterminated -> ERR unterminated quoted string
```

# Testing Steps and Results

Focused V0.2 test script:

```bash
python tests/test_v0_2.py
```

Regression test script:

```bash
python tests/test_v0_1.py
```

These tests compile `server.cpp` with `parser.cpp`, start the server, and exercise it through real TCP connections.

Result:

```text
v0.1 socket tests passed
v0.2 tokenizer tests passed
```

They were run in a disposable Linux Docker container because the local Windows MinGW compiler does not provide POSIX socket headers.

# Known Limitations

- The wire protocol is still newline-delimited plain text, not RESP.
- Quoting is intentionally minimal and not a full shell parser.
- Only `PING`, `SET`, and `GET` are implemented.
- The server still uses one detached thread per client.
- Data remains in memory only and is lost on restart.

# What the Next Version Builds Upon

V1.0 can replace the line tokenizer with a RESP2 decoder that produces the same `vector<string> argv` shape. The command dispatch layer should not need a rewrite when the input protocol changes.
