# Redis_v6.0 Documentation

# Goal

Replace the parser-level `if/else` command chain with a hash-table command registry, and add core utility commands.

# Previous Limitation

`parser.cpp` routed commands through long string comparisons and per-family dispatchers. Adding commands required editing multiple switch chains.

# Concepts

- Redis registers `redisCommand` structs into a dict at startup for O(1) lookup.
- Commands carry arity metadata (+N exact, -N minimum) and flags (`readonly` / `write`) for future AOF and replication hooks.
- `SELECT` isolates keyspaces per client via a logical database index.

# Design Decisions

- `CommandContext` carries the client and all databases; handlers use `ctx.db()` for the selected DB.
- Per-type modules keep their handlers private and export `register*Commands()` only.
- Utility commands live in `commands.cpp`; type commands stay in `cmd_*.cpp`.
- `SCAN` uses a numeric cursor over sorted keys (simplified vs Redis's opaque cursor).

# Files Changed

- `commands.h` / `commands.cpp` — new registry and utility commands.
- `parser.cpp` — slim dispatch wrapper around `executeCommand`.
- `client.h` — `db_index`.
- `eventloop.cpp` — 16 databases, `initCommandTable()` at startup.
- All `cmd_*.cpp` — `register*Commands()` replaces `dispatch*Command()`.
- `tests/test_v6_0.py` — integration tests.

# Commands Added

`ECHO`, `SELECT`, `DBSIZE`, `FLUSHDB`, `FLUSHALL`, `KEYS`, `SCAN`, `RENAME`, `RENAMENX`, `DEBUG SLEEP` (plus existing `PING`, `TYPE`, `DEL`, `EXISTS`, `OBJECT ENCODING` moved into registry).

# Testing

```bash
python tests/test_v6_0.py
```

# Known Limitations

- Single-server process; no `CONFIG`, `INFO`, or cluster commands yet.
- `SCAN` cursor is an offset, not opaque.
- `DEBUG SLEEP` blocks the event loop (intentional for testing).

# Next

V7.0 adds `maxmemory` config and approximated LRU eviction using command write flags.
