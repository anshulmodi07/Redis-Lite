# Redis_v10.1 Documentation

# Goal

Optimistic locking with `WATCH`: detect concurrent key changes between `WATCH` and `EXEC`.

# Flow

1. `WATCH key …` — register keys on current DB
2. Read / decide / `MULTI` / queue writes
3. If another client modified a watched key → `EXEC` returns `*-1` (null), transaction not run
4. Otherwise `EXEC` runs normally and clears watches

Same-client writes before `MULTI` do not set `dirty`.

# State

**Server:** `watched_keys` — `"db|key"` → `{fd, …}`

**Client:** `watches`, `dirty`

# Commands

| Command | Behavior |
|---------|----------|
| `WATCH key …` | `+OK`; errors if inside `MULTI` |
| `EXEC` (dirty) | `*-1`, clear queue + watches |

# Files

- `multi.cpp` — watch registry, `notifyWriteKeys()`, `watchCleanup()`
- `client.h` — `dirty`, `watches`
- `parser.cpp` — `keyPositions()` exported for key extraction

# Testing

```bash
python tests/test_v10_1.py
```

# Next

V11.0 — pipelining verification + benchmark.
