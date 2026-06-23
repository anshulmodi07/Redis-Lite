# Project Structure

Current version: V3.1

```text
.
|-- client.cpp          # One-command TCP client for manual checks
|-- client.h            # Per-connection event-loop client state
|-- cmd_string.cpp      # String command handlers (SET, INCR, MGET, ...)
|-- cmd_string.h        # String command dispatch declaration
|-- eventloop.cpp       # epoll reactor and typed in-memory database
|-- eventloop.h         # Event-loop entry point
|-- object.cpp          # RedisObject lifecycle, int/raw string encoding
|-- object.h            # Typed value wrapper (type, encoding, ptr)
|-- parser.cpp          # Tokenizer and top-level command routing
|-- parser.h            # Parser and dispatch declarations
|-- resp.cpp            # RESP2 decoder and response encoders
|-- resp.h              # RespParser and RESP encoder declarations
|-- server.cpp          # Listening socket setup and event-loop startup
|-- docs/version/Redis_v3.1.md
`-- tests/test_v3_1.py  # V3.1 string command regression tests
```

## File Responsibilities

- `object.h` / `object.cpp` — `RedisObject`, `ENC_RAW` / `ENC_INT` string storage, `tryParseInteger()`, `getStringValue()`, `setStringInteger()`, `setStringValue()`.
- `cmd_string.h` / `cmd_string.cpp` — all string-type commands; `dispatchStringCommand()` is the string entry point.
- `parser.cpp` — routes string commands to `cmd_string`; keeps `PING`, `TYPE`, `DEL`, `EXISTS`.
- `eventloop.cpp` — unchanged reactor; `Db` is `unordered_map<string, RedisObject*>`.
