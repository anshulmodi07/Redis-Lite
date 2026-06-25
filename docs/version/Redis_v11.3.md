# Redis_v11.3 Documentation

# Goal

Hash-slot sharding with CRC16, `MOVED` redirects, and basic `CLUSTER` admin commands.

# Concepts

- **16384 slots** — `slot = CRC16(key) % 16384`; hash tags `{tag}` use only the tag for CRC16.
- **MOVED** — write/read for a foreign slot returns `-MOVED <slot> <ip>:<port>`.
- **CLUSTER MEET** — register a remote node and complementary slot ranges for redirects.

# CLI

```text
--cluster-enabled
--cluster-id <40-char-id>
--cluster-slots <start-end>   # repeatable; default 0-16383
--cluster-announce-ip <ip>
--cluster-bus-port <port>     # default client_port + 10000 (stub bus)
```

# Commands

| Command | Behavior |
|---------|----------|
| `CLUSTER MYID` | Configured or auto-generated node ID |
| `CLUSTER INFO` | Basic cluster state text |
| `CLUSTER NODES` | One-line node description |
| `CLUSTER MEET ip port` | Register remote node + slot map hint |
| `CLUSTER SETSLOT slot NODE\|MIGRATING\|IMPORTING …` | Slot ownership admin |

# Files

- `cluster.cpp` / `cluster.h` — slot table, MOVED, CLUSTER handlers
- `commands.cpp` — `clusterPreflight()` before command dispatch
- `parser.cpp` — `keyPositions()` includes `EVAL` / `EVALSHA` keys

# Testing

```bash
python tests/test_v11_3.py
```

Two clustered nodes with split slot ranges; `CLUSTER MYID` and `MOVED` on wrong-node SET.

Result: passed (WSL).

# Known limitations

- Gossip bus (`clusterPeriodic`) is a stub; no real PING/PONG/FAIL propagation.
- No `ASK`, slot migration (`MIGRATE`), or client-side slot cache.
- `CLUSTER MEET` uses simplified slot assignment for test redirects.

# Next

V12 — expanded INFO/CONFIG, benchmarks, resume packaging.
