# Project Structure

Current version: V4.2

```text
|-- db.h                # RedisDb, expiry helpers, active expiry cycle, and monotonic clock
|-- cmd_zset.cpp        # Sorted set command handlers (ZADD, ZRANGE, ZRANK, ...)
|-- cmd_zset.h
|-- skiplist.cpp        # Ordered ZSET index API over score/member pairs
|-- skiplist.h
|-- cmd_set.cpp / cmd_list.cpp / cmd_hash.cpp / cmd_string.cpp
|-- object.cpp
|-- parser.cpp
`-- tests/test_v4_2.py
```

## File Responsibilities

- `db.h` - shared `RedisDb`, expiry metadata type, lazy/active expiry helpers, and `nowMs()`.
- `cmd_zset.h` / `cmd_zset.cpp` - sorted set commands on `OBJ_ZSET`.
- `skiplist.h` / `skiplist.cpp` - ordered score/member index plus member score lookup helpers for sorted sets.
- `cmd_set.h` / `cmd_set.cpp` - set commands on `unordered_set<string>` inside `OBJ_SET`.
- `object.cpp` - creates and destroys string/list/hash/set/zset backing objects.
- `parser.cpp` - lazily expires touched keys, then routes command families to command modules.
- `eventloop.cpp` - owns the process-wide DB and runs active expiry every ~100ms.
