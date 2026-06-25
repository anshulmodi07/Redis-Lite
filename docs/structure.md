# Project Structure

Current version: V11.3

```text
Redis Lite/
|-- parser.cpp / commands.cpp / resp.cpp   # protocol + dispatch
|-- object.cpp / encoding.cpp              # RedisObject + encodings
|-- cmd_*.cpp                              # data-type commands
|-- eviction.cpp / server_config.cpp       # ServerConfig + CLI flags
|-- rdb.cpp / aof.cpp                      # persistence
|-- pubsub.cpp / multi.cpp                 # pub/sub + transactions
|-- sha1.cpp                               # SCRIPT LOAD / EVALSHA digests
|-- scripting.cpp                          # Lua 5.1 EVAL / EVALSHA / SCRIPT
|-- replication.cpp                        # PSYNC, backlog, replica stream
|-- cluster.cpp                            # hash slots, MOVED, CLUSTER *
|-- server.cpp / eventloop.cpp             # startup + epoll loop
|-- third_party/lua-5.1.5/                 # bundled Lua 5.1 sources
`-- tests/
    |-- build_sources.py                   # compile helper (bundled Lua)
    |-- test_v11_0.py … test_v11_3.py
    `-- .build/                            # cached Lua object files
```

## File Responsibilities

- `eviction.h` — `ServerConfig` (maxmemory, port, replica, cluster fields) and `parseServerArgs()`.
- `server_config.cpp` — CLI parsing for `--port`, `--replicaof`, `--cluster-*`.
- `scripting.cpp` — Lua VM, `redis.call` / `redis.pcall`, `EVAL`, `EVALSHA`, `SCRIPT`.
- `replication.cpp` — master/replica handshake, RDB full sync, write stream, `READONLY`.
- `cluster.cpp` — CRC16 slots, `MOVED`, `CLUSTER MYID/MEET/INFO/NODES/SETSLOT`.
- `rdb.cpp` — `serializeRDB()` / `loadRDBFromBuffer()` for replication transfer.
- `tests/build_sources.py` — compiles bundled Lua as C and links with the server binary.
