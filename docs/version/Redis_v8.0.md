# Redis_v8.0 Documentation

# Goal

Persist the full in-memory dataset to a binary RDB snapshot and restore it on server startup. `SAVE` blocks the event loop until the file is written.

# Previous Limitation

V7 kept all state in RAM only. A restart wiped every key, TTL, and logical database.

# Concepts

**RDB** is Redis’s point-in-time snapshot format: a length-prefixed binary stream, not text.

```
REDIS0011
[ FE db_index ]
[ FB key_count expire_count ]        # resize hint
[ FC expiry_ms ]? type key value ...   # per key
FF
8-byte CRC64
```

- **Opcodes** (`0xFE` SELECTDB, `0xFC` EXPIRETIME_MS, `0xFF` EOF) frame metadata vs key records.
- **Type bytes** (0–4) select string/list/set/zset/hash encoders for the value blob.
- **Length encoding** reuses Redis-style 6/14/32-bit lengths and `C0`/`C1`/`C2` integer shortcuts for compact strings.
- **Blocking SAVE**: single-threaded loop cannot serve clients during `saveRDB()` — acceptable for V8.0; V8.1 adds `BGSAVE` via `fork()`.

# Design Decisions

| Choice | Rationale |
|--------|-----------|
| Version `REDIS0011` | Matches guide; enough structure for all five types without RDB9+ module metadata |
| CRC64 footer | Detect truncated/corrupt files on load |
| `g_rdb_filename = "dump.rdb"` | Default path; loaded in `runEventLoop()` if present |
| Skip empty DBs on save | Smaller files; loader switches DB via `SELECTDB` opcodes |
| Collection encoding | Count + elements (lists/sets/hashes); zset uses `double` + member pairs |
| `SAVE` is `CMD_READONLY` | No keyspace mutation; skips maxmemory pre-check |

# Files

| File | Role |
|------|------|
| `rdb.h` / `rdb.cpp` | `saveRDB`, `loadRDB`, wire encoding/decoding, CRC |
| `commands.cpp` | `SAVE` command |
| `eventloop.cpp` | `loadRDB()` before accepting connections |
| `tests/probe_v8_0.cpp` | Offline save/load roundtrip |
| `tests/test_v8_0.py` | SAVE + process restart integration test |

# Commands

- `SAVE` — writes `dump.rdb`, returns `+OK` or `-ERR rdb save failed`

# Testing

```bash
python tests/test_v8_0.py
```

Probe: string + integer + hash roundtrip. Integration: SET/HSET → SAVE → restart → GET/HGET.

# Known Limitations

- No `BGSAVE`, AOF, or `LASTSAVE` yet (V8.1–V8.2).
- No `FA` AUX metadata fields.
- Listpack/intset/quicklist encodings are expanded to logical type layout on disk.
- Corrupt RDB fails load silently except a console warning; no partial recovery.

# Next

V8.1 — non-blocking `BGSAVE` using `fork()` + copy-on-write.
