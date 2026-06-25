# Redis_v11.2 Documentation

# Goal

Primary-replica replication: full sync via RDB, then a live stream of write commands.

# Concepts

- **PSYNC** — partial resync from backlog when offset matches; otherwise `+FULLRESYNC` + raw RDB bytes.
- **Replication backlog** — 1 MiB ring of encoded write commands on the master.
- **Read-only replica** — client writes return `READONLY`; reads allowed.

# CLI

```text
--replicaof <host> <port>   # start as replica of master
```

Replica sends `REPLCONF listening-port` and `PSYNC ? -1` on connect.

# Files

- `replication.cpp` / `replication.h` — handshake, backlog, stream apply, `INFO replication`
- `rdb.cpp` — `serializeRDB()` / `loadRDBFromBuffer()`
- `commands.cpp` — `replicationFeedWrite()` after writes; READONLY only for write commands
- `eventloop.cpp` — master link fd in epoll; `replicationSetContext(..., epoll_fd)`

# Design decisions

- Custom full-sync line: `+FULLRESYNC <replid> <offset> <rdb_len>\r\n` then RDB payload.
- Replica parser skips `+OK` / other simple strings before `+FULLRESYNC` / command stream.
- Master wakes replica client `EPOLLOUT` when appending replication data.

# Testing

```bash
python tests/test_v11_2.py
```

Master on 8080, replica on 8081 with `--replicaof 127.0.0.1 8080`; verifies GET after SET and READONLY on replica write.

Result: passed (WSL).

# Known limitations

- No diskless fork+BGSAVE; snapshot is in-memory `serializeRDB`.
- Partial resync is best-effort; no replica ACK tracking.
- No `SLAVEOF`/`REPLICAOF` admin commands at runtime.

# Next

V11.3 — cluster hash slots and MOVED redirects.
