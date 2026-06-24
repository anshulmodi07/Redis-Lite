# Redis_v4.3 Documentation

# Goal

Add the TTL command family and wire `SET`/`SETEX` expiry options to the shared expiry map.

# Previous Limitation

V4.2 could delete expired keys but clients had no way to set or inspect TTLs.

# Concepts

- Expiry timestamps are stored as absolute milliseconds in `expires`.
- Relative commands (`EXPIRE`, `PEXPIRE`, `SET EX/PX`) convert to absolute time at write time.
- Absolute commands (`EXPIREAT`, `PEXPIREAT`, `SET EXAT/PXAT`) store the given timestamp directly.
- `TTL`/`PTTL` return `-2` for missing keys and `-1` for keys without expiry.
- `PERSIST` removes expiry metadata without deleting the value.
- `SET` without a TTL option clears any existing expiry on the key.

# Files Changed

- `db.h` - expiry helpers (`setExpireAtMs`, `removeExpire`, `ttlSeconds`, `ttlMilliseconds`).
- `cmd_expire.h` / `cmd_expire.cpp` - `EXPIRE`, `PEXPIRE`, `EXPIREAT`, `PEXPIREAT`, `TTL`, `PTTL`, `PERSIST`.
- `cmd_string.cpp` - wires `SET EX/PX/EXAT/PXAT` and `SETEX` to the expiry map.
- `parser.cpp` - routes TTL commands, clears expiry on `DEL`, lazy-expires TTL command keys.
- `tests/test_v4_3.py` - probe and server integration tests.

# Behavior

Clients can now set, inspect, and remove key TTLs. `SET k v ex N` expires the key after N seconds.

# Testing

```bash
python3 tests/test_v4_3.py
python3 tests/test_v4_2.py
python3 tests/test_v4_1.py
```

# Known Limitations

- Only string `SET`/`SETEX` set expiry inline; other write commands do not accept TTL options yet.
- `nowMs()` uses wall-clock time so NTP adjustments can affect absolute expiry comparisons.

# Next

V5.0 starts compact internal encodings with SDS strings.
