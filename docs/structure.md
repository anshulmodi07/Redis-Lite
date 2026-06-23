# Project Structure

Current version: V4.0

```text
|-- db.h                # RedisDb, expiry metadata alias, and monotonic millisecond clock
|-- cmd_zset.cpp        # Sorted set command handlers (ZADD, ZRANGE, ZRANK, ...)
|-- cmd_zset.h
|-- skiplist.cpp        # Ordered ZSET index API over score/member pairs
|-- skiplist.h
|-- cmd_set.cpp / cmd_list.cpp / cmd_hash.cpp / cmd_string.cpp
|-- object.cpp
|-- parser.cpp
`-- tests/test_v4_0.py
```

## File Responsibilities

- `db.h` - shared `RedisDb`, expiry metadata type, and `nowMs()` helper for TTL versions.
- `cmd_zset.h` / `cmd_zset.cpp` - sorted set commands on `OBJ_ZSET`.
- `skiplist.h` / `skiplist.cpp` - ordered score/member index plus member score lookup helpers for sorted sets.
- `cmd_set.h` / `cmd_set.cpp` - set commands on `unordered_set<string>` inside `OBJ_SET`.
- `object.cpp` - creates and destroys string/list/hash/set/zset backing objects.
- `parser.cpp` - routes command families to their command modules.
- `eventloop.cpp` - owns the process-wide keyspace and expiry metadata map.
