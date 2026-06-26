# Redis Lite — Future Performance Improvements (Phase 2)

Following the successful implementation of the Phase 1 performance optimizations, this document outlines the next set of architectural and algorithmic improvements designed to close the throughput gap with Real Redis.

---

## 1. O(1) Amortized RESP Parser Consumption (Read Path)
* **Problem**: Currently, after every successfully parsed command in a pipeline, `RespParser::tryParse` calls `buffer_.erase(0, next)`. For a pipeline of size $P$, this causes O(N) memory moves for the remaining buffer content on every command, resulting in an $O(P^2)$ scaling bottleneck on the read path.
* **Solution**: Maintain a read cursor `head_` in `RespParser` and parse relative to `head_`. Only erase or compact the buffer when `head_` exceeds a memory threshold (e.g. 64 KB) or when the buffer is fully drained. This turns parser consumption into an $O(1)$ amortized operation.

---

## 2. Fast O(1) Direct Client Lookups (Event Loop)
* **Problem**: In the hot path of the epoll event loop, client objects are looked up using `clients.find(fd)` on a `std::unordered_map`. While average lookup is $O(1)$, hash mapping introduces hashing computation, bucket traversals, and cache misses.
* **Solution**: Maintain a flat lookup array of pointers `Client* fast_clients[SOMAXCONN]` (e.g. 65,536 pointers). Since file descriptors are dense, small integers, looking up a client becomes a single direct array index operation `fast_clients[fd]`, eliminating hash-map overhead entirely.

---

## 3. TCP Socket Buffer Size Tuning
* **Problem**: Accepted client sockets default to OS-defined send and receive buffer sizes. Under high pipeline depth (e.g. P=256), the server can experience early TCP write blocking, leading to socket backpressure.
* **Solution**: Explicitly set socket send and receive buffers to `128 KB` using `setsockopt` with `SO_SNDBUF` and `SO_RCVBUF` on all accepted client sockets, ensuring high-throughput capacity.
