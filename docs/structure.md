# Project Structure

Current version: V6.0

```text
|-- commands.h / commands.cpp  # command registry, arity checks, utility commands
|-- client.h                   # per-connection state (fd, db_index, parser, write_buf)
|-- sds.h / sds.cpp
|-- listpack.h / listpack.cpp
|-- intset.h / intset.cpp
|-- encoding.h / encoding.cpp
|-- db.h                       # RedisDb, expiry helpers, active expiry cycle
|-- cmd_*.h / cmd_*.cpp        # per-type handlers registered into commands.cpp
|-- parser.cpp                 # tokenize + lazy expiry + executeCommand dispatch
|-- eventloop.cpp              # epoll loop, 16 logical databases
`-- tests/test_v6_0.py
```

## File Responsibilities

- `commands.h` / `commands.cpp` — `Command` metadata, `commandTable` hash map, `executeCommand`, and utility commands (`ECHO`, `SELECT`, `KEYS`, `SCAN`, `RENAME`, `DEBUG SLEEP`, ...).
- `client.h` — connection state including selected database index.
- `parser.cpp` — RESP path still flows through `dispatch()` which applies lazy expiry then calls `executeCommand`.
- `cmd_*.cpp` — each module exports `register*Commands()` to populate the central table.
- `eventloop.cpp` — owns `vector<RedisDb>` (16 DBs), initializes the command table at startup.
