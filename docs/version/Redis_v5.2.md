# Redis_v5.2 Documentation

# Goal

Add an intset module: a sorted integer array at the narrowest fixed width that fits all members, for compact integer-only sets.

# Previous Limitation

V5.1 added listpack for sequential collections, but sets still always used `unordered_set<string>` with per-member heap allocations.

# Concepts

- An intset stores members as a sorted array of `int16`, `int32`, or `int64` values depending on range.
- Lookup and insert position use binary search (`O(log N)`).
- When a new value exceeds the current width, the entire array is upgraded and rewritten at the wider encoding.
- Integer-only membership is a common set pattern; intset avoids hash-table overhead for small numeric sets.

# Design Decisions

- Implemented the guide API as a standalone module; set commands still use `unordered_set` until V5.3 wires intset in as `ENC_INTSET`.
- Encoding width is stored as byte size (`2`, `4`, `8`) matching Redis's intset layout.
- `intsetAdd` / `intsetRemove` return the possibly reallocated intset pointer after upgrades or resizes.

# Files Changed

- `intset.h` / `intset.cpp` — intset allocator, sorted insert/remove, binary search, and width upgrade.
- `tests/probe_v5_2.cpp` / `tests/test_v5_2.py` — unit probe plus set command regression.

# Behavior

No wire-protocol change in this version. The intset module is ready for V5.3 to adopt as compact set storage for integer members.

# Testing

```bash
python tests/test_v5_2.py
```

Results: intset probe passes; set command server regression passes in Linux/WSL.

# Known Limitations

- Set command handlers do not use intset yet; non-integer members still go through `unordered_set`.
- No shrink/downgrade path when large values are removed.
- `OBJECT ENCODING` is not exposed until V6.0.

# Next

V5.3 wires listpack/intset in as default encodings and auto-promotes to full structures at Redis thresholds.
