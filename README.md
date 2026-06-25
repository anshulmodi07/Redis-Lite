# Redis Lite

Redis Lite is a lightweight, single-threaded, high-performance in-memory key-value database built in C++17 using the `epoll` reactor pattern. It supports the RESP2 wire protocol, transactions, pub/sub, expiry, and replication.

## Key Features
- **epoll Reactor Loop**: Single-threaded event loop for handling thousands of concurrent connections.
- **RESP2 Protocol**: Native compatibility with standard clients (e.g., `redis-cli`).
- **Core Data Types**: Strings, Hashes, Lists, Sets, and Sorted Sets.
- **Compact Encodings**: Simple Dynamic Strings (SDS), listpacks, and intsets for optimized memory usage.
- **Durability**: RDB snapshotting (via fork-based copy-on-write) and Append-Only File (AOF) with customizable fsync policies.
- **Transactions & Pub/Sub**: Atomic command queuing via MULTI/EXEC/DISCARD and channel messaging.
- **Primary-Replica Replication**: Handshake, full sync (RDB), and command stream mirroring.

## Benchmarks

The following table compares the performance of Redis Lite on a standard Linux/WSL environment under sequential (no pipeline) and pipelined workloads:

| Scenario | Redis Lite | Real Redis | Gap / Explanation |
|---|---|---|---|
| **GET / SET (no pipeline)** | ~794 ops/sec | 100k - 120k ops/sec | Bound by TCP round-trip and syscall overhead. |
| **GET / SET (pipeline=16)** | ~11.3k ops/sec | 800k+ ops/sec | Pipelining reduces syscall overhead by ~14x; remains limited by user-space string allocation copies and no custom memory allocator (like jemalloc). |
| **ZADD Leaderboard** | ~800 ops/sec | 80k - 100k ops/sec | Custom skip list management overhead compared to highly optimized Redis C structures. |

*Note: Real Redis leverages highly optimized C memory layouts, jemalloc allocator, TCP buffer tuning, and inline pipeline optimizations.*

## Compilation & Run

To compile and launch the server in a Linux/WSL environment:

```bash
# Compile and run the test suite
python3 tests/test_v12.py

# Run the benchmark
python3 tests/benchmark.py
```
