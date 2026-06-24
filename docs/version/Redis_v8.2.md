# Redis_v8.2 Documentation

# Goal

Log every write command as RESP to `appendonly.aof` and rebuild the dataset on restart by replaying those commands.

# How It Works

```
Write command succeeds
    └─► aofAppendCommand(argv)  →  in-memory aof_buf
Event loop tick (every ~100ms)
    └─► aofFlush()  →  fwrite to appendonly.aof
    └─► aofPeriodic()  →  fdatasync every 1s (default everysec behavior)
Startup
    └─► if AOF exists: aofLoad() replays via executeCommand (g_aof_replaying=true)
    └─► else: loadRDB() as before
```

AOF takes precedence over RDB when both files exist — AOF is the more complete durability log.

# Design

| Piece | Detail |
|-------|--------|
| `g_aof_replaying` | Suppresses append during replay (no recursive logging) |
| Write hook | `executeCommand` appends after `cmd.func` for `CMD_WRITE` |
| `encodeArray(argv)` | Native RESP command serialization |
| `aof_fd` | `O_APPEND` — crash-safe append without rewriting file |
| fsync | Hard-coded everysec via `fdatasync` each second (CONFIG policies → V8.3) |

# Files

- `aof.h` / `aof.cpp` — init, append, flush, periodic fsync, load/replay
- `commands.cpp` — post-write `aofAppendCommand`
- `eventloop.cpp` — `aofInit`, AOF-before-RDB load order, `aofPeriodic` each loop
- `tests/test_v8_2.py` — write → verify file → restart → read back

# Testing

```bash
python tests/test_v8_2.py
```

# Limitations

- No `appendonly yes/no` CONFIG, `always`/`no` fsync, or `BGREWRITEAOF` (V8.3).
- `SELECT` not logged — replay assumes db 0 unless SELECT added to AOF later.
- Failed writes still append (simplified; Redis logs attempted mutating commands in transactions only).

# Next

V8.3 — CONFIGurable fsync policies + `BGREWRITEAOF` compaction.
