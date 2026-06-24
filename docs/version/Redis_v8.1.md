# Redis_v8.1 Documentation

# Goal

Non-blocking snapshots via `fork()` + copy-on-write: parent keeps serving while the child writes `dump.rdb`.

# How It Works

```
Client ──BGSAVE──► parent forks
                    ├─ child: saveRDB() → _exit(0)
                    └─ parent: +Background saving started (immediate)
Event loop polls waitpid(WNOHANG) each tick → clears in-progress flag
```

At `fork()`, the child sees a frozen memory image. Parent writes only trigger COW page copies — the child's snapshot stays consistent.

# Design

| Piece | Detail |
|-------|--------|
| `startBgsave()` | `fork()`; child calls `saveRDB` + `_exit` (not `exit`) |
| `checkBgsaveChild()` | Called from epoll loop; `waitpid(..., WNOHANG)` |
| `BGSAVE` | Returns `+Background saving started` or `ERR` if already running |
| `SAVE` | Unchanged — still blocking |
| Platform | `fork` only on Linux/macOS; elsewhere `startBgsave` fails |

# Files

- `rdb.h` / `rdb.cpp` — `bgsaveInProgress`, `startBgsave`, `checkBgsaveChild`
- `commands.cpp` — `BGSAVE` command
- `eventloop.cpp` — reap child each iteration
- `tests/test_v8_1.py` — non-blocking PING + restart restore + duplicate BGSAVE ERR

# Testing

```bash
python tests/test_v8_1.py
```

# Limitations

- No `LASTSAVE` / `INFO persistence` yet.
- Duplicate BGSAVE while child runs returns ERR (no queue).
- Windows has no `fork` (WSL/Linux only).

# Next

V8.2 — AOF append + replay on startup.
