# Redis_v9.0 Documentation

# Goal

Channel pub/sub: clients subscribe to channels, publishers push messages without storing them in the keyspace.

# Architecture

```
channel_to_clients:  channel → {fd, …}
client_channels:     fd → {channel, …}
client_patterns:     fd → {glob, …}
```

`PUBLISH` queues `["message", channel, payload]` to each subscriber's `write_buf` and arms `EPOLLOUT`. Pattern subs get `["pmessage", pattern, channel, payload]`.

Subscribers enter `pubsub_mode` — only `(P)SUBSCRIBE`, `(P)UNSUBSCRIBE`, `PING` allowed.

# Commands

| Command | Reply |
|---------|-------|
| `SUBSCRIBE ch …` | per channel: `["subscribe", ch, total]` |
| `UNSUBSCRIBE [ch …]` | `["unsubscribe", ch, total]` |
| `PUBLISH ch msg` | `:N` subscriber count |
| `PSUBSCRIBE pat …` | `["psubscribe", pat, total]` |
| `PUNSUBSCRIBE [pat …]` | `["punsubscribe", pat, total]` |
| `PUBSUB CHANNELS [pat]` | array of active channels |
| `PUBSUB NUMSUB [ch …]` | flat `ch count …` pairs |

# Files

- `pubsub.h` / `pubsub.cpp` — registry + commands
- `client.h` — `pubsub_mode`
- `commands.h` — `CommandContext` gains `clients*`, `epoll_fd`
- `eventloop.cpp` — passes client map; `pubsubCleanup` on disconnect

# Testing

```bash
python tests/test_v9_0.py
```

# Next

V10.0 — `MULTI` / `EXEC` / `DISCARD` transactions.
