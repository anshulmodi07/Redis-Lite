# Redis Lite

A single-threaded, in-memory key-value store implementing the Redis wire protocol (RESP),
built in C++17 with an epoll-based reactor event loop.

## Features

- **RESP2 protocol** — works with real `redis-cli` and Redis client libraries
- **Core data types:** String, Hash, List, Set, Sorted Set
- **Compact encodings:** SDS, listpacks, intsets for optimized memory usage
- **TTL/expiry:** active sweep (100ms cycle) + passive (lazy) expiry
- **Persistence:** RDB snapshotting (fork + COW) and AOF logging (configurable fsync)
- **Pub/Sub** and **MULTI/EXEC transactions**
- **Lua scripting** (EVAL/EVALSHA/SCRIPT) via bundled Lua 5.1
- **Primary-replica replication:** handshake, full sync (RDB), command stream mirroring
- **Basic clustering:** CRC16 hash slots, MOVED redirection, CLUSTER commands

## Performance (honest numbers, as of 2026-06-25)

Benchmarked with `redis-benchmark -n 100000` on WSL2 (Linux 6.18), same machine for both servers.

### Baseline — no pipelining

| Command | Redis Lite | Real Redis | Gap |
|---------|-----------|------------|-----|
| **SET** | ~4.5k req/s | ~76k req/s | ~17× |
| **GET** | ~25k req/s | ~78k req/s | ~3× |

### Pipeline scaling (P=16)

| Command | Redis Lite | Real Redis | Gap |
|---------|-----------|------------|-----|
| **SET** | ~10.4k req/s | ~625k req/s | ~60× |
| **GET** | ~6.5k req/s | ~1.06M req/s | ~163× |

**Known limitation:** GET throughput degrades under high concurrency (C≥50) with deep pipelining —
see [docs/analysis.md](docs/analysis.md) for full root-cause analysis and next steps in
[docs/improve.md](docs/improve.md).

> The gap vs Real Redis is expected: Redis uses jemalloc, highly tuned C structures, kernel buffer
> tuning, and years of micro-optimizations. Redis Lite is a learning implementation in C++17.

## Architecture

See [docs/design_doc.md](docs/design_doc.md) for the full architecture diagram and design
rationale (why single-threaded, why skip lists for ZSET, fork+COW for snapshotting, etc.)

## Build from source

Requires a Linux/WSL environment with `g++` (C++17) and Python 3.

```bash
# Compile the server (builds bundled Lua and links everything)
python3 tests/build_sources.py

# Run the test suite
python3 tests/test_v12.py

# Run the benchmark
python3 tests/benchmark.py
```

## Project history

This was built incrementally from a thread-per-client toy TCP server (V0) to a full-featured
single-threaded epoll-reactor server implementing the RESP protocol (V12). See
[docs/structure.md](docs/structure.md) and [docs/guide.md](docs/guide.md) for the full
version-by-version build log.
