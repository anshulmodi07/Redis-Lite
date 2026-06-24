# Redis_v7.0 Documentation

# Goal

Add `maxmemory` limits and approximated LRU eviction, plus `CONFIG GET/SET` for memory settings.

# Previous Limitation

V6 had no memory cap; the server could grow without bound and had no eviction path.

# Concepts

- 24-bit `lru` field on `RedisObject`, updated on access (`now_seconds & 0xFFFFFF`).
- Approximated LRU: sample N random keys, evict the oldest `lru` — O(samples) per eviction.
- Policies: `noeviction`, `allkeys-lru`, `volatile-lru`, plus random/TTL/LFU enum stubs for future work.
- Write commands check memory before running; `noeviction` returns Redis-style OOM.

# Design Decisions

- Memory usage is estimated by scanning keyspace (good enough for learning; not jemalloc-precise).
- Eviction samples one non-empty DB per cycle, then N keys from the policy pool.
- `CONFIG SET maxmemory` parses `kb`/`mb` suffixes; `0` disables the limit.
- `ensureMemoryForWrite` hooks into `executeCommand` for `CMD_WRITE` commands.

# Files Changed

- `eviction.h` / `eviction.cpp` — config, estimation, eviction engine.
- `object.h` / `object.cpp` — `lru` field; touch on create.
- `commands.cpp` — `CONFIG` command; pre-write memory guard.
- `parser.cpp` — touch on key access.
- `tests/build_sources.py` — adds `eviction.cpp` to canonical sources.
- `tests/test_v7_0.py` — eviction + OOM tests.

# Testing

```bash
python tests/test_v7_0.py
```

# Known Limitations

- LFU policies are registered but use LRU scoring until a dedicated frequency counter is added.
- Memory estimate is coarse; eviction may trigger early/late vs real RSS.
- No `INFO memory` section yet (V12).

# Next

V8.0 adds blocking RDB `SAVE` snapshot persistence.
