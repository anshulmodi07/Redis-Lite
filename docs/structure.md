# Project Structure

Current version: V3.2

```text
.
|-- cmd_hash.cpp        # Hash command handlers (HSET, HGET, HGETALL, ...)
|-- cmd_hash.h
|-- cmd_string.cpp      # String command handlers
|-- cmd_string.h
|-- object.h / object.cpp
|-- parser.cpp          # Top-level command routing
|-- eventloop.cpp       # epoll reactor + Db
`-- tests/test_v3_2.py
```

## File Responsibilities

- `cmd_hash.h` / `cmd_hash.cpp` — hash commands backed by `unordered_map<string,string>` inside `OBJ_HASH` objects; `dispatchHashCommand()` entry point.
- `parser.cpp` — routes `HSET`/`HGET`/… to `cmd_hash`.
- `object.cpp` — `createHashObject()` allocates the backing hash table.
