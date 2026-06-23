# Redis_v4.0 Documentation

# Goal

Add expiry metadata storage as the foundation for TTL support.

# Previous Limitation

The keyspace only stored `key -> RedisObject*`; there was no parallel metadata table for expiration timestamps.

# Concepts

- Redis keeps expiry metadata outside the object itself.
- `Expires` maps `key -> expiry_timestamp_ms`.
- `RedisDb` groups the main keyspace and expiry metadata for later expiry checks.
- `nowMs()` returns a monotonic millisecond timestamp for future relative TTL work.

# Files Added or Changed

- `db.h` - `RedisDb`, expiry metadata alias, and `nowMs()` helper.
- `eventloop.cpp` - owns the process-wide `Expires expires` map next to the main keyspace.
- `tests/test_v4_0.py` - verifies metadata helper behavior and existing server commands.
- `docs/structure.md` - updated project map.

# Behavior

No user-visible command behavior changes in V4.0. Expiry is only stored as infrastructure for V4.1+.

# Testing

```bash
python3 tests/test_v4_0.py
python3 tests/test_v3_5.py
```

# Known Limitations

- No lazy deletion yet.
- No active expiry loop yet.
- No TTL command family yet.

# Next

V4.1 uses the expiry map to lazily delete expired keys on access.
