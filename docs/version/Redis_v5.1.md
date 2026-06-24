# Redis_v5.1 Documentation

# Goal

Add a listpack module: a single contiguous byte buffer for small collections instead of heap-scattered nodes.

# Previous Limitation

V5.0 only compacted string values (SDS). Hash, list, and sorted-set objects still used separate heap allocations per element (`unordered_map`, `std::list`, skip-list nodes).

# Concepts

- A listpack is a flat byte array with a 6-byte header (total bytes + element count) and a `0xFF` sentinel.
- Each entry stores a backward length prefix, an encoded payload, and supports forward/backward traversal without extra pointers.
- Simplified encodings: 7-bit unsigned integers (`0xxxxxxx`), short strings up to 63 bytes (`10xxxxxx` + data), long strings (`0xE0` + 32-bit length + data), and 64-bit integers (`0xF0` + value).
- One listpack with a handful of entries can fit in a single cache line.

# Design Decisions

- Implemented the guide's public API (`lpNew`, `lpAppend`, `lpFirst`/`lpNext`/`lpPrev`, `lpGet`, `lpLength`, `lpFree`) as a standalone module.
- Hash/list/zset command handlers still use their existing full structures; wiring listpack in as the default encoding and auto-promotion thresholds belong to V5.3.
- `lpAppendInteger()` is provided alongside `lpAppend()` because integer encoding is a core listpack optimization.

# Files Changed

- `listpack.h` / `listpack.cpp` — listpack allocator, append, iteration, and decode helpers.
- `tests/probe_v5_1.cpp` / `tests/test_v5_1.py` — unit probe plus hash/list/zset regression over the server.

# Behavior

No wire-protocol change in this version. The listpack module is available for V5.3 to adopt as `ENC_LISTPACK` backing storage.

# Testing

```bash
python tests/test_v5_1.py
```

Results: listpack probe passes; hash/list/zset server regression passes in Linux/WSL.

# Known Limitations

- No `lpInsert`, `lpDelete`, or `lpReplace` yet; V5.3 may add helpers when hash fields need in-place updates.
- Sets still use `unordered_set`; intset arrives in V5.2.
- `OBJECT ENCODING` is not exposed until V6.0.

# Next

V5.3 wires listpack and intset in as default compact encodings with auto-promotion thresholds.
