# Redis_v3.1 Documentation

# Goal and Motivation

V3.1 completes the string command family on top of V3.0's `RedisObject` wrapper. Values can now be stored as raw bytes or as native integers, and clients get the full set of Redis string operations (`INCR`, `MSET`, `APPEND`, conditional `SET`, etc.).

# Previous Limitation Being Fixed

V3.0 only supported bare `SET`/`GET`. Every value was a heap-allocated `std::string` even when the payload was numeric, so counters required parse-on-read. There was no multi-key get/set, no append, and no conditional writes.

# Concepts Taught

- **Integer encoding (`ENC_INT`)**: values that fully parse as `long long` are stored as `long long*` in `RedisObject::ptr`, not as decimal text. `INCR`/`DECR` mutate the integer in place — O(1) without `stoll` on every op.
- **Encoding transparency**: `GET` always returns the decimal string via `getStringValue()`, whether the backing store is `ENC_RAW` or `ENC_INT`.
- **Single-threaded atomicity**: the epoll loop processes one command at a time, so `INCR`, `GETSET`, and `MSET` need no mutex.
- **Conditional SET**: `NX` / `XX` return null bulk (`$-1`) when the condition fails instead of an error.

# Design Decisions and Trade-Offs

- String commands live in `cmd_string.cpp` instead of `parser.cpp` so later types get their own `cmd_*.cpp` modules (V3.2 hash, etc.).
- `SET EX` / `PX` / `SETEX` parse expiry arguments but do not enforce TTL until V4 wires the expiry map.
- `MGET` returns `WRONGTYPE` if any requested key holds a non-string type (matches Redis).
- `INCR` on a missing key starts from 0 (stored as `ENC_INT`), yielding 1.
- `APPEND` always produces a string result via `setStringValue()`, which may re-encode as `ENC_INT` if the merged value is a pure integer.

# Code Walkthrough

## Integer detection — `object.cpp`

```cpp
bool tryParseInteger(const string& value, long long& out) {
    // stoll with full-string consumption — rejects "3.14", "12abc"
}

RedisObject* createStringObject(const string& value) {
    if (tryParseInteger(value, integer))
        return { OBJ_STRING, ENC_INT, new long long(integer) };
    return { OBJ_STRING, ENC_RAW, new string(value) };
}
```

`getStringValue()` branches on `encoding`: `to_string(*int_ptr)` for `ENC_INT`, dereference `string*` for `ENC_RAW`.

## In-place counter updates — `cmd_string.cpp`

```cpp
RedisObject* ensureStringKey(Db& db, const string& key) {
    // missing key -> create ENC_INT 0
    // wrong type -> nullptr -> WRONGTYPE
}

commandIncrBy:
    readStringInteger(obj, current)  // works on ENC_INT or parseable ENC_RAW
    setStringInteger(obj, current + delta)
    return encodeInteger(next)
```

## Conditional SET — `cmd_string.cpp`

```cpp
struct SetOptions { bool nx, xx; bool has_ex, has_px; long long ex, px; };

parseSetOptions(argv, opts)   // scans tokens after key/value
canSetKey(db, key, opts)      // NX blocks existing keys, XX blocks missing keys
failed condition -> encodeNullBulk()   // not an error
```

## Dispatch routing — `parser.cpp`

```cpp
if (cmd is SET|GET|INCR|...|STRLEN)
    return dispatchStringCommand(normalized, db);
```

# Files Added or Changed

| File | Change |
|------|--------|
| `object.h` / `object.cpp` | `ENC_INT`, `tryParseInteger`, `readStringInteger`, `setStringInteger`, `setStringValue`, `stringObjectLength` |
| `cmd_string.h` / `cmd_string.cpp` | **new** — 14 string commands |
| `parser.cpp` | removed inline SET/GET; routes string commands |
| `tests/test_v3_1.py` | **new** — incr, append, setnx, NX, mset/mget, getset |
| `tests/test_v*.py` | link `cmd_string.cpp` |
| `docs/structure.md`, `docs/guide.md` | version bump |

# Commands Added

| Command | Arity | Reply |
|---------|-------|-------|
| `SET key value [EX s] [PX ms] [NX] [XX]` | ≥3 | `+OK` or null bulk |
| `GET key` | 2 | bulk or nil |
| `GETSET key value` | 3 | old bulk/nil |
| `MSET k v [k v ...]` | odd, ≥3 | `+OK` |
| `MGET k [k ...]` | ≥2 | array of bulk/nil |
| `SETNX key value` | 3 | `:1` or `:0` |
| `SETEX key seconds value` | 4 | `+OK` |
| `INCR` / `DECR key` | 2 | integer |
| `INCRBY` / `DECRBY key delta` | 3 | integer |
| `APPEND key value` | 3 | new length |
| `STRLEN key` | 2 | length (`0` if missing) |

# Testing

```bash
python3 tests/test_v3_1.py
python3 tests/test_v3_0.py   # regression
```

Guide manual checks:

```bash
redis-cli -p 8080 set counter 0
redis-cli -p 8080 incr counter      # 1
redis-cli -p 8080 incrby counter 5  # 6
redis-cli -p 8080 append mykey "hello"
redis-cli -p 8080 append mykey " world"
redis-cli -p 8080 strlen mykey      # 11
redis-cli -p 8080 set k v NX        # OK
redis-cli -p 8080 set k v2 NX       # (nil)
```

# Known Limitations

- `EX` / `PX` / `SETEX` accepted but expiry not enforced until V4.
- No `SET` `GET` flag (Redis 6.2+).
- Integer overflow on `INCR` not checked.
- Hash/list/set/zset commands still absent (V3.2+).

# What V3.2 Builds On

Hash commands (`HSET`, `HGET`, …) will use `createHashObject()` and the same `WRONGTYPE` pattern established here.
