# Redis_v12 Documentation

## Goal and Motivation
The goal of V12 is to introduce complete observability via an expanded `INFO` command, support runtime inspection via `CONFIG GET *`, evaluate the performance of our single-threaded event loop under standard and pipelined workloads, and compile comprehensive packaging artifacts (Architecture Diagram, Design Document, and benchmarks).

## Previous Limitations
- The `INFO` command was stubbed and did not include key database statistics such as uptime, memory consumption, connected clients, and operations per second.
- `CONFIG GET` only allowed querying one hardcoded config parameter at a time and did not support wildcard parameter matching (`CONFIG GET *`).
- There were no baseline or pipelining benchmarks recorded in the repository.

## Concepts Taught
- **Uptime tracking**: Computing system uptime relative to server boot time.
- **Client tracking**: Querying active epoll client connections dynamically.
- **Memory footprint estimation**: Combining custom object-specific memory size calculation (`estimateServerMemory`) with process-level RSS queries from `/proc/self/status` VmRSS line.
- **Instantaneous operations tracking**: Sampling executed command counts over a sliding $1$ second window to yield real-time ops/sec metrics.
- **Performance benefits of pipelining**: Understanding how combining multiple commands into a single TCP write decreases kernel transition overhead.

## Design Decisions and Trade-offs
- **Instantaneous ops/sec**: Instead of taking a simple average over the lifetime of the process, we sample commands processed every $1000$ ms. This yields a dynamic and accurate reflection of active traffic.
- **Human-readable RSS vs Used Memory Ratio**: The memory fragmentation ratio uses VmRSS divided by estimated logical database memory. This is standard in Redis, though since our database memory is an estimate rather than a direct allocator count, the ratio is a useful approximation.

## Files Added or Changed
- [MODIFY] [commands.h](file:///d:/Redis%20Lite/commands.h) - Declared `ServerStats` and `g_stats`.
- [MODIFY] [commands.cpp](file:///d:/Redis%20Lite/commands.cpp) - Defined `g_stats`, updated `executeCommand` command counter, implemented `INFO` sections (Server, Clients, Memory, Stats), and added `CONFIG GET *`.
- [MODIFY] [eventloop.cpp](file:///d:/Redis%20Lite/eventloop.cpp) - Added connection counter, initialized start time, and implemented the periodic ops/sec sample timer.
- [NEW] [test_v12.py](file:///d:/Redis%20Lite/tests/test_v12.py) - Automated tests for INFO and CONFIG.
- [NEW] [benchmark.py](file:///d:/Redis%20Lite/tests/benchmark.py) - Benchmark client script.
- [NEW] [design_doc.md](file:///d:/Redis%20Lite/docs/design_doc.md) - Conceptual interview design document.

## Behavior and Commands Added
- `INFO [section]` - Displays key server telemetry. Sections: `server`, `clients`, `memory`, `stats`, `replication`, `keyspace`.
- `CONFIG GET *` - Returns a flat array of all configuration key-value pairs.

## Testing Steps and Results
- Automated validation:
  ```bash
  wsl python3 tests/test_v12.py
  ```
  Result: `all tests passed`
- Performance benchmarks:
  ```bash
  wsl python3 tests/benchmark.py
  ```
  Result:
  - Sequential (no pipeline): ~794 ops/sec
  - Pipelined (P=16): ~11,357 ops/sec (approx $14\times$ throughput improvement)

## Known Limitations
- VmRSS retrieval relies on Linux `/proc/self/status` and will return 0 or fallback if run on non-Linux environments.
- Blocked clients metric is always reported as 0 since blocking commands (e.g. BLPOP) are not implemented.

## What the Next Version Builds Upon
This is the final release version in this guide sequence. Future iterations would build on top of these telemetry features to implement clustering cluster rebalancing, custom memory allocators (like jemalloc), and multithreaded I/O threads.
