# Redis_v3.2 Documentation

# Goal

Implement the Hash type: a field→value map under one Redis key, with full hash command coverage and `WRONGTYPE` guards.

# Previous Limitation

V3.1 only had string keys. No nested field/value structure, no `HSET`/`HGET`, no type mismatch errors on hash commands.

# Concepts

- **Hash backing store**: `OBJ_HASH` + `ENC_HASHTABLE` → `unordered_map<string, string>*` in `RedisObject::ptr`.
- **WRONGTYPE**: any hash command on a non-hash key returns `-WRONGTYPE Operation against a key holding the wrong kind of value`.
- **Missing key semantics**: `HGET`/`HMGET` return nil for missing fields; `HLEN`/`HEXISTS` return 0; `HSET`/`HINCRBY` create the hash lazily.

# Code Walkthrough

## Hash lookup helper — `cmd_hash.cpp`

```cpp
HashMap* lookupHash(Db& db, const string& key, bool create, bool& type_error) {
    // missing + create  -> createHashObject(), insert into db
    // missing + !create -> nullptr
    // exists + wrong type -> type_error = true
    // exists + OBJ_HASH   -> cast ptr to HashMap*
}
```

Every mutating command (`HSET`, `HDEL`, `HINCRBY`) calls `lookupHash(..., create=true)`. Read commands check type explicitly and return `WRONGTYPE` or empty/nil as appropriate.

## HSET field counting

```cpp
for each field/value pair:
    if field not in hash -> added++
    hash[field] = value
return encodeInteger(added)   // Redis 4.0+ style: new fields only
```

`HMSET` aliases the same handler.

## HINCRBY

```cpp
lookupHash(key, create=true)
current = tryParseInteger(existing) or 0 if field missing
hash[field] = to_string(current + delta)
return encodeInteger(next)
```

Non-integer field values return `ERR hash value is not an integer`.

## WRONGTYPE after type overwrite — guide test

```bash
SET user:1 "oops"    # destroys hash object, stores string
HGET user:1 name     # -WRONGTYPE
```

`cmd_string.cpp` `putString()` calls `destroyObject()` on the old key, so the hash table is freed before the string is stored.

# Commands

| Command | Notes |
|---------|-------|
| `HSET` / `HMSET` | variadic field/value pairs; returns count of **new** fields |
| `HGET` | nil if key or field missing |
| `HMGET` | array with nil slots for missing fields |
| `HDEL` | integer count removed |
| `HEXISTS` | 1/0; WRONGTYPE if key is non-hash |
| `HLEN` | 0 if key missing |
| `HKEYS` / `HVALS` / `HGETALL` | array reply; empty array if key missing |
| `HINCRBY` | creates field at 0 if absent |

# Files

- `cmd_hash.h` / `cmd_hash.cpp` — **new**
- `parser.cpp` — hash command routing
- `tests/test_v3_2.py` — **new**

# Testing

```bash
python3 tests/test_v3_2.py
python3 tests/test_v3_1.py   # regression
```

# Known Limitations

- Hash iteration order is undefined (`unordered_map`).
- Listpack compact encoding deferred to V5.1.
- No TTL on hash fields until V4.

# Next: V3.3

List type with `std::list<string>` backing store and `LPUSH`/`RPUSH`/`LRANGE` commands.
