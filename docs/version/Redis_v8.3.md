# Redis_v8.3 Documentation

# Goal

Configurable AOF durability (`appendfsync`) and compact background rewrite (`BGREWRITEAOF`).

# appendfsync Policies

| Policy | Behavior |
|--------|----------|
| `always` | `write` + `fdatasync` after every write command |
| `everysec` | `fdatasync` once per second (default) |
| `no` | kernel buffers only; OS decides flush timing |

Set via `CONFIG SET appendfsync <policy>`; read with `CONFIG GET appendfsync`.

# BGREWRITEAOF

```
Parent forks → child writes appendonly.aof.rewrite (compact RESP: one command per key state)
             → rename() over appendonly.aof → _exit
Parent reopens AOF fd on success
```

Compact encoding examples:
- String → `SET key val [PX ttl]`
- Hash → `HSET key f1 v1 …`
- List → `RPUSH key …`
- Set → `SADD key …`
- ZSet → `ZADD key score member …`

Mutually exclusive with `BGSAVE` / another `BGREWRITEAOF`.

# Files

- `aof.h` / `aof.cpp` — fsync policy, `writeCompactAof`, fork rewrite, `checkBgrewriteChild`
- `commands.cpp` — `CONFIG appendfsync`, `BGREWRITEAOF`
- `rdb.cpp` — `startBgsave` blocks if rewrite running
- `eventloop.cpp` — `checkBgrewriteChild` each tick

# Testing

```bash
python tests/test_v8_3.py
```

# Limitations

- No `appendonly yes/no` toggle; AOF always on when fd opens.
- Rewrite uses current in-memory state; concurrent writes during rewrite append to new inode until parent reopens fd.
- Windows: no `fork` for BGREWRITEAOF.

# Next

V9.0 — Pub/Sub (`SUBSCRIBE` / `PUBLISH`).
