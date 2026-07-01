[![CI](https://github.com/anshulmodi07/Redis-Lite/actions/workflows/ci.yml/badge.svg)](https://github.com/anshulmodi07/Redis-Lite/actions/workflows/ci.yml)
[![Docker](https://github.com/anshulmodi07/Redis-Lite/actions/workflows/docker.yml/badge.svg)](https://github.com/anshulmodi07/Redis-Lite/actions/workflows/docker.yml)

# Redis Lite

A single-threaded, in-memory key-value store implementing the Redis wire protocol (RESP),
built in C++17 with an epoll-based reactor event loop.

## Features
- RESP2 protocol — works with real `redis-cli` and Redis client libraries
- Core data types: String, Hash, List, Set, Sorted Set
- TTL/expiry (active + passive)
- Persistence: RDB snapshotting (fork + COW) and AOF logging
- Pub/Sub, MULTI/EXEC transactions
- Lua scripting (EVAL/EVALSHA) via bundled Lua 5.1
- Basic clustering (hash slots, MOVED, CLUSTER commands)

## Quick start
```bash
docker run -p 8080:8080 devam246/redis-lite:v1.0.0
redis-cli -p 8080 PING
```

## Live demo
```bash
redis-cli -h 16.192.114.182 -p 8080 PING
```

## Performance

**Confirmed baseline** (from `docs/analysis.md`, standardized `redis-benchmark` methodology):

| Scenario | Redis Lite | Real Redis | Gap |
|---|---|---|---|
| SET, no pipeline | ~4.5k req/s | ~76k req/s | ~17× |
| GET, no pipeline | ~25k req/s | ~78k req/s | ~3× |
| SET, pipeline P=16 | ~10.4k req/s | ~625k req/s | ~60× |
| GET, pipeline P=16 | ~6.5k req/s | ~600k req/s | ~92× |

**Latest run** (`redis-benchmark -t set,get -n 100000 -c 50 -P 16 -q`) — *preliminary, pending 3-run verification before this replaces the table above*:

| Metric | Result |
|---|---|
| SET Throughput | 127,388.53 req/s |
| GET Throughput | 228,832.95 req/s |
| SET p50 Latency | 6.103 ms |
| GET p50 Latency | 3.367 ms |

This run is ~10–12× higher than the confirmed baseline above and appears to contradict the
documented concurrency limitation below. Before treating it as canonical: rerun it 2 more
times and confirm results land within ~5% of each other, and confirm the comparison is
against `docs/analysis.md` (not the archived `docs/history/2026-06-replication-backlog-fix.md`
report, which used an older, non-standardized method).

**Known limitation:** GET throughput has historically degraded under high concurrency
(C≥50) with deep pipelining — see [docs/analysis.md](docs/analysis.md) for root-cause
analysis and [docs/improve.md](docs/improve.md) for the fix roadmap. If the verification
runs above confirm the new numbers, update this section to reflect the fix rather than
deleting the limitation silently.

## Architecture
See [docs/design_doc.md](docs/design_doc.md) for the full architecture diagram and design
rationale (why single-threaded, why skip lists for ZSET, fork+COW for snapshotting, etc.)

## Build from source
```bash
python3 tests/build_sources.py
```

## Project history
Built incrementally from a thread-per-client toy server to V12. See
[docs/structure.md](docs/structure.md) and [docs/guide.md](docs/guide.md) for the full
version-by-version build log, and [docs/history/](docs/history/) for superseded reports.
