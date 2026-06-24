# Project Structure

Current version: V9.0

```text
|-- pubsub.h / pubsub.cpp   # SUBSCRIBE/PUBLISH/PSUBSCRIBE/PUBSUB
|-- client.h                # pubsub_mode flag
|-- eventloop.cpp           # client map passed to dispatch; cleanup on disconnect
`-- tests/test_v9_0.py
```

## File Responsibilities

- `pubsub.cpp` — channel/pattern maps; push messages to subscriber `write_buf`.
- `commands.cpp` — blocks non-pubsub commands when `client.pubsub_mode`.
- `eventloop.cpp` — `clientWritePending()` for publish-side `EPOLLOUT`.
