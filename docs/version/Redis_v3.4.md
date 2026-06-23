# Redis_v3.4 Documentation

# Goal

Implement the Set type: unordered unique strings with membership tests and set algebra.

# Previous Limitation

V3.3 added ordered lists but no uniqueness or set operations (`SINTER`, `SUNION`, etc.).

# Concepts

- **Backing store**: `OBJ_SET` → `unordered_set<string>*`.
- **Set algebra**: `SINTER` (all keys), `SUNION` (any key), `SDIFF` (first minus rest). Missing keys are empty sets.
- **Random ops**: `SPOP` / `SRANDMEMBER` use `rand() % size` + `std::advance`.
- **WRONGTYPE**: any existing non-set key in a multi-key op aborts with `WRONGTYPE`.

# Code Walkthrough

## lookupSet / getSet

```cpp
lookupSet(db, key, create=true)   // SADD creates set if missing
getSet(db, key)                   // read-only; nullptr if missing
```

## Set algebra helpers

```cpp
setIntersection(sets)  // empty if any input missing/empty
setUnion(sets)         // skips nullptr keys
setDifference(sets)    // first set minus all others
```

`SINTERSTORE dst k1 k2` calls `storeSetMembers(db, dst, result)` — destroys any prior object at `dst`.

## Random member selection

```cpp
randomMember(set):
    it = set.begin(); advance(it, rand() % set.size())

randomMembers(set, count):
    count > 0  -> sample without replacement (up to set size)
    count < 0  -> sample with replacement |count| times
```

## SADD return value

Returns count of **new** members inserted (`set.insert().second`).

# Commands

`SADD`, `SREM`, `SMEMBERS`, `SCARD`, `SISMEMBER`, `SMISMEMBER`, `SPOP`, `SRANDMEMBER`, `SINTER`, `SUNION`, `SDIFF`, `SINTERSTORE`, `SUNIONSTORE`, `SDIFFSTORE`

# Testing

```bash
python3 tests/test_v3_4.py
python3 tests/test_v3_3.py   # regression
```

Guide flow:

```bash
redis-cli sadd s1 a b c d
redis-cli sadd s2 c d e f
redis-cli sinter s1 s2    # c d
redis-cli sunion s1 s2    # a b c d e f
redis-cli sdiff s1 s2     # a b
redis-cli scard s1        # 4
```

# Known Limitations

- Member order from `SMEMBERS` is undefined.
- `intset` compact encoding deferred to V5.2.
- No `SSCAN` until V6.

# Next: V3.5

Sorted sets with skip list + score hash map.
