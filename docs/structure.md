# Project Structure

Current version: V8.3

```text
|-- aof.h / aof.cpp       # AOF log, appendfsync policies, BGREWRITEAOF compaction
|-- rdb.h / rdb.cpp       # RDB + BGSAVE (excludes concurrent rewrite)
|-- commands.cpp          # CONFIG appendfsync, BGREWRITEAOF
`-- tests/test_v8_3.py
```

## File Responsibilities

- `aof.cpp` — `g_aof_fsync_policy`; `always` fsyncs per write; `everysec` in `aofPeriodic`; `writeCompactAof` + `fork` for rewrite.
- `commands.cpp` — `CONFIG GET/SET appendfsync`; `BGREWRITEAOF` returns `+Background append only file rewriting started`.
- `eventloop.cpp` — `checkBgrewriteChild()` reopens AOF after successful rewrite.
