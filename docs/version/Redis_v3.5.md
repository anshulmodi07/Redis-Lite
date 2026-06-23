# Redis_v3.5 Documentation

# Goal

Implement sorted sets (`OBJ_ZSET`) for leaderboard-style score/member data.

# Previous Limitation

V3.4 added unordered sets, but there was no score-ordered collection for rank, range, or pop-by-score operations.

# Concepts

- ZSET keeps two synchronized views: `member -> score` and an ordered score/member index.
- Score ties sort lexicographically by member.
- Missing zsets behave like empty collections for read/remove/pop commands.
- Existing non-zset keys return `WRONGTYPE`.

# Files Added or Changed

- `skiplist.h` / `skiplist.cpp` - ordered index API for score/member pairs.
- `cmd_zset.h` / `cmd_zset.cpp` - ZSET command handlers.
- `object.cpp` - creates and destroys `ZSet` objects.
- `parser.cpp` - routes sorted set commands.
- `tests/test_v3_5.py` - focused command and regression flow.
- Older test harnesses - include the current zset sources so parser dispatch links during regression runs.
- `docs/structure.md` - updated current structure.

# Commands

`ZADD`, `ZRANGE`, `ZRANGEBYSCORE`, `ZREVRANGE`, `ZRANK`, `ZREVRANK`, `ZSCORE`, `ZCARD`, `ZCOUNT`, `ZREM`, `ZINCRBY`, `ZPOPMIN`, `ZPOPMAX`

# Testing

```bash
python3 tests/test_v3_5.py
python3 tests/test_v3_4.py
```

# Known Limitations

- The ordered index is implemented with C++ ordered containers behind the `SkipList` API rather than a hand-rolled probabilistic skip list.
- Score ranges are inclusive; Redis-exclusive bounds like `(1` are not implemented yet.
- Advanced Redis options such as `ZRANGE ... STORE` are not included.

# Next

V4 starts production hardening with expiry metadata and TTL behavior.
