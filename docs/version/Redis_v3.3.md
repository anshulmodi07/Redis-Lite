# Redis_v3.3 Documentation

# Goal

Implement the List type with O(1) push/pop at both ends using `std::list<string>`, plus range and mutation commands.

# Previous Limitation

V3.2 added hashes but no ordered sequence type. No push/pop, no index access, no list slicing.

# Concepts

- **Doubly-linked list**: `OBJ_LIST` + `ENC_QUICKLIST` tag (full `std::list` for now; compact quicklist in V5).
- **Negative indices**: `LRANGE key 0 -1` returns all elements; `-1` is the last item.
- **Duplicates allowed**: same value can appear at multiple positions.
- **WRONGTYPE**: list commands on non-list keys return the standard error.

# Code Walkthrough

## Index normalization — `cmd_list.cpp`

```cpp
long long normalizeIndex(long long index, long long size) {
    if (index < 0) index = size + index;
    return index;
}
```

`LRANGE` / `LTRIM` then clamp: `start < 0 → 0`, `stop >= size → size-1`, empty result if `start > stop`.

## Push order

```cpp
LPUSH key a b c  -> push_front each -> list reads [c, b, a]
RPUSH key a b c  -> push_back each  -> list reads [a, b, c]
```

## LRANGE slice

Walk the list once, collect elements where `start <= index <= stop`, return `encodeArray(slice)`.

## LREM count semantics

| count | direction |
|-------|-----------|
| `> 0` | remove from head, at most `count` matches |
| `< 0` | remove from tail, at most `|count|` matches |
| `0` | remove all matches |

## lookupList

Same pattern as `lookupHash`: create on write (`LPUSH`/`RPUSH`), `nullptr` + `WRONGTYPE` on type mismatch, missing key reads return nil/0/empty array.

# Commands

`LPUSH`, `RPUSH`, `LPOP`, `RPOP` (optional count), `LLEN`, `LRANGE`, `LINDEX`, `LSET`, `LINSERT`, `LREM`, `LTRIM`

# Files

- `cmd_list.h` / `cmd_list.cpp` — **new**
- `parser.cpp` — list routing
- `tests/test_v3_3.py` — **new**

# Testing

```bash
python3 tests/test_v3_3.py
python3 tests/test_v3_2.py   # regression
```

Guide flow:

```bash
redis-cli -p 8080 rpush mylist a b c
redis-cli -p 8080 lrange mylist 0 -1   # a b c
redis-cli -p 8080 lpush mylist z
redis-cli -p 8080 lrange mylist 0 -1   # z a b c
redis-cli -p 8080 lrange mylist 1 2    # a b
redis-cli -p 8080 lpop mylist          # z
redis-cli -p 8080 llen mylist          # 3
```

# Known Limitations

- Index access is O(N) via `std::list` traversal.
- Quicklist/listpack encoding deferred to V5.
- `LINSERT` scans for first pivot match only.

# Next: V3.4

Set type with `unordered_set<string>` and `SADD`/`SINTER`/etc.
