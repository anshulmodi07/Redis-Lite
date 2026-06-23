# Redis_v4.2 Documentation

# Goal

Add active expiry so expired keys can be removed even when clients stop reading them.

# Previous Limitation

V4.1 only deleted expired keys when a command touched them.

# Concepts

- `activeExpireCycle()` samples up to 20 expiry keys.
- Expired sample keys are removed from both `data` and `expires`.
- If at least 25% of the sample expired, the cycle immediately samples again.
- The epoll loop wakes about every 100ms to run the cycle.

# Files Changed

- `db.h` - shared expiry helpers and active expiry cycle.
- `eventloop.cpp` - uses a 100ms epoll timeout and runs active expiry.
- `parser.cpp` - reuses shared `expireIfNeeded()`.
- `tests/test_v4_2.py` - direct active expiry probe.
- `docs/structure.md` - current version update.

# Behavior

Expired keys no longer need a direct read to be cleaned eventually. No TTL commands are added yet.

# Testing

```bash
python3 tests/test_v4_2.py
python3 tests/test_v4_1.py
python3 tests/test_v3_5.py
```

# Known Limitations

- Clients still cannot set expiries with commands until V4.3.
- Sampling uses the current hash iteration order instead of random sampling.

# Next

V4.3 adds `EXPIRE`, `TTL`, `PERSIST`, and related commands.
