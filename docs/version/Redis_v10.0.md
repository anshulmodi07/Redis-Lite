# Redis_v10.0 Documentation

# Goal

Redis-style transactions: commands after `MULTI` are queued, then run back-to-back on `EXEC`.

# Concepts

- **Atomicity** — single-threaded event loop; `EXEC` runs the whole queue without yielding.
- **No isolation** — other clients can read intermediate state between your queued writes.
- **No rollback** — runtime errors on one queued command do not stop the rest.

# Per-client state

```cpp
bool in_multi;
bool multi_error;  // set when a queued command fails validation
vector<vector<string>> queued_commands;
```

# Commands

| Command | Behavior |
|---------|----------|
| `MULTI` | Enter multi mode, reply `+OK` |
| `…` (in multi) | Validate, push argv, reply `+QUEUED` or `-ERR` |
| `EXEC` | If `multi_error` → `EXECABORT`; else run queue, reply array of per-command RESP |
| `DISCARD` | Clear queue and exit multi mode |

`EXEC` / `DISCARD` without `MULTI` → error. Nested `MULTI` → error.

Invalid or wrong-arity commands in multi set `multi_error` but are still queued; `EXEC` aborts the whole batch.

# Files

- `multi.h` / `multi.cpp` — `tryTransaction()`
- `client.h` — transaction fields
- `resp.cpp` — `encodeRespArray()` (nested RESP elements, not bulk-wrapped)
- `commands.h` — `CommandContext::exec_replay`

# Testing

```bash
python tests/test_v10_0.py
```

# Next

V10.1 — `WATCH` optimistic locking.
