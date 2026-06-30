# Performance Optimization & Benchmarking Report

This report documents the performance diagnosis, optimization, and final benchmark results for the **Redis-Lite** project.

---

## 1. Executive Summary

During pipelined benchmark testing, we identified a significant performance gap between read and write operations:
*   **GET (Pipelined):** ~85,763 requests per second (rps)
*   **SET (Pipelined):** ~13,431 requests per second (rps)

By analyzing the write command path, we diagnosed a critical **O(N) memory-shifting bottleneck** in the replication backlog. After implementing an amortized circular-buffer-like trimming strategy, we achieved a **4.5× performance increase** for write commands, bringing `SET` throughput to **61,349.70 rps** and pushing `GET` throughput to **110,741.97 rps**.

---

## 2. Diagnosis & Root Cause Analysis

### The Write Command Path
Every write command executed by the server (such as `SET`) is forwarded to the replication engine to keep connected replicas in sync. This calls `replicationFeedWrite()`, which appends command bytes to the master replication backlog buffer:

```cpp
// replication.cpp
void backlogAppend(const string &bytes) {
  repl_backlog.append(bytes);
  if (repl_backlog.size() > REPL_BACKLOG_SIZE) {
    const size_t trim = repl_backlog.size() - REPL_BACKLOG_SIZE;
    repl_backlog.erase(0, trim);
    repl_backlog_start += trim;
  }
  g_master_repl_offset += static_cast<long long>(bytes.size());
}
```

### The Bottleneck
1.  `REPL_BACKLOG_SIZE` is defined as `1024 * 1024` (1 MB).
2.  Once the backlog fills up to 1 MB, every subsequent command append (usually ~40–50 bytes) triggers the condition `repl_backlog.size() > REPL_BACKLOG_SIZE`.
3.  `std::string::erase(0, trim)` has **O(N) time complexity**, forcing the CPU to shift the remaining ~1,000,000 bytes in memory to the left.
4.  For a benchmark of 100,000 commands, this resulted in **over 73,000 shift operations of 1 MB**, causing **more than 75 Gigabytes of memory copies** inside the event loop. This completely starved the socket of CPU cycles, creating a massive write throughput bottleneck.

---

## 3. Implementation Details

We optimized `backlogAppend()` in [replication.cpp](file:///c:/Summer%2026/redis-project/Redis-Lite/replication.cpp) to amortize the cost of trimming. Instead of erasing and shifting memory on every single write command, we allow the backlog buffer to grow up to `2 * REPL_BACKLOG_SIZE` (2 MB) and perform a single 1 MB trim only when it exceeds that threshold:

```diff
 void backlogAppend(const string &bytes) {
   repl_backlog.append(bytes);
-  if (repl_backlog.size() > REPL_BACKLOG_SIZE) {
+  if (repl_backlog.size() > 2 * REPL_BACKLOG_SIZE) {
     const size_t trim = repl_backlog.size() - REPL_BACKLOG_SIZE;
     repl_backlog.erase(0, trim);
     repl_backlog_start += trim;
   }
 
   g_master_repl_offset += static_cast<long long>(bytes.size());
 }
```

### Why this is highly efficient:
*   **Amortized Complexity:** Instead of shifting 1 MB of memory for every command (~40 bytes), the memory shift happens only once every 1 MB of appends (approx. every 25,000 commands).
*   **99.99% Shift Reduction:** The total memory copies required for a 100k command suite dropped from **~75 GB** to **less than 4 MB**.
*   **Memory Bound:** A strict memory limit is still enforced, capping the backlog size at a maximum of 2 MB.

---

## 4. Benchmark Results

The following table compares the performance of Redis-Lite before and after the optimization using the standard `redis-benchmark` tool (`-n 100000 -c 50 -P 16`):

| Metric | Before Optimization | After Optimization | Improvement |
| :--- | :--- | :--- | :--- |
| **SET Throughput** | 13,431.83 rps | **61,349.70 rps** | **+356.7% (4.5×)** |
| **GET Throughput** | 85,763.29 rps | **110,741.97 rps** | **+29.1% (1.3×)** |
| **SET p50 Latency** | 56.96 ms | **12.34 ms** | **78.3% lower latency** |
| **GET p50 Latency** | 8.82 ms | **6.89 ms** | **21.9% lower latency** |

> [!NOTE]
> The GET throughput also improved from ~85k to ~110k rps because the event loop is no longer bogged down by memory compaction overheads, allowing faster scheduling of client read/write events.

---

## 5. Conclusion

By addressing the `O(N)` string buffer erase bottleneck in the replication backlog, we successfully unlocked the full potential of pipelining in **Redis-Lite**. Write command execution is now extremely fast and does not block the single-threaded event loop, making the server capable of handling over 60k write rps and 110k read rps on a standard developer machine.
