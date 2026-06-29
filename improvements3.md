# Redis Lite — Future Performance Improvements (Phase 3)

This document outlines next-level optimizations (Phase 3) that can be implemented to close the remaining memory allocation and runtime execution gaps with Real Redis.

---

## 1. Vector and String Buffer Reuse (Zero-Allocation Parser Path)
* **Current Bottleneck**: Every parsed RESP command constructs a new `std::vector<std::string>` vector and new `std::string` objects for all command arguments (e.g., command name, keys, values). This triggers heavy allocator activity under high load.
* **Proposed Optimization**:
  - Store a reusable `std::vector<std::string> parsed_argv_cache` inside the `Client` structure.
  - When parsing a command, reuse this vector and its existing `std::string` element capacities by calling `.clear()` rather than reallocating. This eliminates heap allocations for command parsing entirely.

---

## 2. Linking with `jemalloc` (Memory Management)
* **Current Bottleneck**: Standard glibc `malloc`/`free` incurs overhead from global lock contention and size-class fragmentation during rapid allocations.
* **Proposed Optimization**:
  - Link the compilation process statically or dynamically with `jemalloc` (`-ljemalloc`).
  - `jemalloc` uses thread-specific arenas and thread-local caches, which typically boosts write-heavy execution throughput by **10%–20%**.
  - Installation requirement: `sudo apt-get install libjemalloc-dev`.

---

## 3. Pre-Allocated Common Error RESP Strings
* **Current Bottleneck**: Error replies (e.g., wrong arity, invalid type errors) currently construct new strings at runtime (e.g. `encodeError("ERR wrong number of arguments for...")`).
* **Proposed Optimization**:
  - Maintain a cache of static error responses for common protocol errors inside the `Resp` namespace.
  - Return references to these static strings instead of allocating new heap strings on error pathways.

---

## 4. Hash Table Buckets Pre-sizing
* **Current Bottleneck**: The database's `std::unordered_map` bucket arrays grow dynamically, triggering rehashing spikes when keys are added in large batches during benchmarks.
* **Proposed Optimization**:
  - Pre-reserve bucket slots in the database `std::unordered_map` (e.g., `db.data.reserve(100000)`) during initialization or benchmark warmups to eliminate rehashing latency spikes.
