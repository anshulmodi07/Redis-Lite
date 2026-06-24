# Project Structure

Current version: V8.0

```text
|-- rdb.h / rdb.cpp           # RDB snapshot save/load, CRC64, default dump.rdb path
|-- commands.cpp              # SAVE command; CONFIG from V7
|-- eventloop.cpp             # load dump.rdb on startup before epoll loop
|-- eviction.cpp              # maxmemory + LRU (V7)
|-- tests/build_sources.py    # includes rdb.cpp in CORE_SOURCES
`-- tests/test_v8_0.py
```

## File Responsibilities

- `rdb.cpp` — encodes all five types to `REDIS0011` format; `loadRDB` replaces in-memory state after CRC check.
- `eventloop.cpp` — if `dump.rdb` exists at boot, calls `loadRDB()` before `initCommandTable()` client traffic.
- `commands.cpp` — `SAVE` invokes `saveRDB(g_rdb_filename, ctx.databases)` (blocks entire server).

## Test Baseline (V6+)

All server tests use `tests/build_sources.py`. V8 adds `rdb.cpp` to `CORE_SOURCES`.
