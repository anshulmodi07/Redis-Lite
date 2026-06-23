# Redis_v4.1 Documentation

# Goal

Delete expired keys lazily when commands access them.

# Previous Limitation

V4.0 stored expiry timestamps but never applied them.

# Concepts

- `isExpired()` checks the expiry map against `nowMs()`.
- `expireIfNeeded()` removes both the key object and expiry metadata.
- Parser-level key collection expires touched keys before dispatching to existing command handlers.

# Files Changed

- `parser.h` / `parser.cpp` - added `dispatch(argv, RedisDb&)` and lazy expiry helpers.
- `eventloop.cpp` - dispatches against `RedisDb`.
- `tests/test_v4_1.py` - direct dispatch probe for expired string and set keys.
- `docs/structure.md` - current version update.

# Behavior

Expired keys behave like missing keys once accessed. No TTL commands are added yet.

# Testing

```bash
python3 tests/test_v4_1.py
python3 tests/test_v4_0.py
python3 tests/test_v3_5.py
```

# Known Limitations

- Expired keys that are never accessed remain until V4.2 active expiry.
- Users still cannot set TTLs via commands until V4.3.

# Next

V4.2 adds periodic active expiry sampling.
