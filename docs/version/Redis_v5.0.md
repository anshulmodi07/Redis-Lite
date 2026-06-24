# Redis_v5.0 Documentation

# Goal

Replace `std::string` backing storage for raw string objects with an SDS-inspired dynamic string that tracks length in O(1) and supports pre-allocated growth for append-heavy workloads.

# Previous Limitation

V4.3 stored `ENC_RAW` strings as heap-allocated `std::string*`. Every `APPEND` built a new temporary string and reallocated from scratch. Length required either scanning or an extra copy through `getStringValue()`.

# Concepts

- SDS keeps a header (`len`, `alloc`) immediately before the character buffer.
- The public `sds` pointer points at the payload, so the buffer stays null-terminated and C-string compatible.
- `sdslen()` reads `len` from the header in O(1) without scanning for `\0`.
- `sdsgrow()` reserves extra capacity so repeated `APPEND` calls can reuse the same allocation.
- Integer encoding (`ENC_INT`) is unchanged; SDS applies only to `ENC_RAW`.

# Design Decisions

- Used a single header layout (`len` + `alloc`) instead of Redis's type-specific `sdshdr5/8/16` variants. This keeps the learning implementation small while preserving the core idea: header-before-data and explicit length tracking.
- `appendStringValue()` calls `sdscatlen()` in place, then re-checks integer encoding so `APPEND` behavior matches the previous `setStringValue()` path.
- `stringObjectLength()` uses `sdslen()` directly for raw strings instead of materializing a `std::string`.

# Files Changed

- `sds.h` / `sds.cpp` — SDS API: `sdsnew`, `sdsfree`, `sdslen`, `sdsgrow`, `sdscat`, `sdscatlen`.
- `object.h` / `object.cpp` — `ENC_RAW` now stores `sds`; added `appendStringValue()`.
- `cmd_string.cpp` — `APPEND` uses `appendStringValue()` instead of copy-then-set.
- `tests/test_v5_0.py` — SDS unit probe plus string command regression.

# Behavior

No wire-protocol change. `SET`, `GET`, `APPEND`, `STRLEN`, and integer commands behave as before. Binary-safe bulk string values are preserved via `sdsnewlen()`.

# Testing

```bash
python tests/test_v5_0.py
```

Results: SDS unit probe and server integration tests pass.

# Known Limitations

- Only string object values use SDS; hash fields, list elements, and set members still use `std::string`.
- No `sdshdr5` packed-header optimization for very short strings.
- `ENC_RAW` is still reported conceptually as a raw string; `OBJECT ENCODING` arrives in V6.

# Next

V5.1 adds listpack encoding for compact small collections.
