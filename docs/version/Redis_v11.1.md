# Redis_v11.1 Documentation

# Goal

Server-side Lua 5.1 scripts with atomic `redis.call()` dispatch.

# Concepts

- **Embedded Lua** — scripts run synchronously on the event-loop thread.
- **KEYS / ARGV** — populated from `EVAL` / `EVALSHA` arguments before `lua_pcall`.
- **SCRIPT cache** — SHA1 digest → source text for `EVALSHA`.

# Commands

| Command | Behavior |
|---------|----------|
| `EVAL script numkeys [keys…] [args…]` | Run script; inner writes via `redis.call` |
| `EVALSHA sha numkeys …` | Run cached script (min 3 argv: cmd, sha, numkeys) |
| `SCRIPT LOAD script` | Store script, return SHA1 bulk string |
| `SCRIPT EXISTS sha …` | Array of 0/1 per digest |
| `SCRIPT FLUSH` | Clear cache |

# Files

- `scripting.cpp` / `scripting.h` — Lua VM, `redis.call`, command registration
- `sha1.cpp` / `sha1.h` — digest for `SCRIPT LOAD`
- `third_party/lua-5.1.5/` — bundled Lua (no system `-llua5.1` required)
- `tests/build_sources.py` — compiles Lua as C and links objects

# Design decisions

- Outer `EVAL` / `EVALSHA` skip AOF/replication feed; inner `redis.call` writes use the normal command path.
- Lua 5.1 success code is `0` (not `LUA_OK` from newer Lua).

# Testing

```bash
python tests/test_v11_1.py
```

Covers conditional lock release via `EVAL` and `SCRIPT LOAD` + `EVALSHA return 42`.

Result: passed (WSL).

# Known limitations

- No `SCRIPT KILL`, sandbox limits, or Redis function library parity.
- Single global Lua state; no per-connection script isolation beyond KEYS/ARGV.

# Next

V11.2 — primary-replica replication.
