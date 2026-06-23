# Redis_v3.0 Documentation

# Goal and Motivation

V3.0 introduces the typed value wrapper that mirrors Redis's `robj`: every key now maps to a `RedisObject*` carrying a type tag, encoding, and pointer to the backing structure instead of a bare `std::string`.

This is the foundation for lists, hashes, sets, and sorted sets in later versions.

# Previous Limitation Being Fixed

V2.1 stored all values as plain strings in `unordered_map<string, string>`. There was no way to represent multiple Redis data types or return `WRONGTYPE` when a command targeted the wrong type.

# Concepts Taught

- Redis `robj` pattern: separate **type** (what commands apply) from **encoding** (how data is stored).
- `void*` with explicit type checks, matching real Redis's C design.
- Factory helpers per type and a centralized `destroyObject()` for correct memory cleanup.
- `TYPE` for introspection and `DEL` for key removal with inner structure freeing.

# Design Decisions and Trade-Offs

- `RedisObject` uses `void*` plus enums rather than `std::variant` to stay close to Redis's layout and prepare for multiple encodings per type later.
- Strings use `OBJ_STRING` + `ENC_RAW` backed by `std::string*`.
- List/hash/set/zset factories create empty STL containers with the encoding tags the guide reserves for later versions (`ENC_QUICKLIST`, `ENC_HASHTABLE`, `ENC_SKIPLIST`).
- `SET` destroys any existing object at the key before inserting a new string object.
- `GET` on a non-string key returns `WRONGTYPE` instead of silently coercing.
- `EXISTS` is included because the guide's V3.0 test flow depends on it; full utility-command coverage still lands in V6.0.

# Files Added or Changed

- `object.h` / `object.cpp` — type/encoding enums, `RedisObject`, factories, `destroyObject()`, `objectTypeName()`, `getStringValue()`.
- `parser.cpp` / `parser.h` — dispatch now takes `unordered_map<string, RedisObject*>&`; `SET`/`GET` use objects; added `TYPE`, `DEL`, `EXISTS`.
- `eventloop.cpp` — database type switched to `RedisObject*`.
- `tests/test_v3_0.py` — regression coverage for type reporting, deletion, existence, and overwrite.
- Older test compile lines updated to link `object.cpp`.
- `docs/structure.md`, `docs/guide.md` checklist, `.gitignore`.

# Behavior and Commands Added

```text
TYPE key              -> +string / +list / +hash / +set / +zset / +none
DEL key [key ...]     -> integer count of keys removed
EXISTS key [key ...]  -> integer count of existing keys
```

Existing commands unchanged in wire behavior:

```text
PING
SET key value
GET key               -> nil for missing keys; WRONGTYPE for non-string keys
```

# Testing Steps and Results

```bash
python3 tests/test_v3_0.py
python3 tests/test_v0_1.py
python3 tests/test_v0_2.py
python3 tests/test_v1_0.py
python3 tests/test_v1_1.py
python3 tests/test_v2_0.py
python3 tests/test_v2_1.py
```

Manual checks from the guide:

```bash
redis-cli -p 8080 set foo bar
redis-cli -p 8080 type foo      # string
redis-cli -p 8080 del foo
redis-cli -p 8080 exists foo    # 0
```

# Known Limitations

- Only string values are populated by commands; other type factories exist but have no command handlers yet.
- No integer encoding (`ENC_INT`) until V3.1.
- No reference counting, LRU field, or eviction.
- Linux/WSL required for build and tests.

# What the Next Version Builds Upon

V3.1 adds the full string command set on top of `RedisObject`, including integer encoding for `INCR`/`DECR` and `SET` options like `NX`/`XX`/`EX`.
