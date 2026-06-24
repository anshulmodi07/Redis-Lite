# Project Structure

Current version: V7.0

```text
|-- commands.h / commands.cpp  # command registry + utility/config commands
|-- eviction.h / eviction.cpp  # maxmemory, approximated LRU eviction, memory estimates
|-- client.h                   # per-connection state (fd, db_index, parser, write_buf)
|-- object.h / object.cpp      # RedisObject with 24-bit lru clock field
|-- tests/build_sources.py     # canonical CORE_SOURCES / SERVER_SOURCES for all tests
|-- tests/dispatch_probe.h     # C++ probe helper for dispatch(client, databases, argv)
`-- tests/test_v7_0.py
```

## File Responsibilities

- `eviction.h` / `eviction.cpp` — `g_server_config`, memory estimation, `touchObject`, eviction policies, `ensureMemoryForWrite`.
- `commands.cpp` — `CONFIG GET/SET` for `maxmemory`, `maxmemory-policy`, `maxmemory-samples`; write commands call `ensureMemoryForWrite` before execution.
- `object.cpp` — sets `lru` on object creation via `touchObject`.
- `parser.cpp` — touches keys on access after lazy expiry.
- `tests/build_sources.py` — **V6+ baseline**: every server/probe link must include `commands.cpp` (and `eviction.cpp` from V7).

## Test Baseline (V6+)

From V6 onward, `dispatch()` requires `Client`, `vector<RedisDb>`, and `commands.cpp`. All `tests/test_v*.py` scripts import `build_sources.py` instead of hand-maintained file lists. C++ probes use `tests/dispatch_probe.h`.
