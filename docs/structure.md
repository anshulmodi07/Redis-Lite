# Project Structure

Current version: V10.1

```text
|-- multi.h / multi.cpp     # MULTI/EXEC/DISCARD + WATCH registry
|-- client.h                # dirty, watches, transaction queue
|-- parser.h / parser.cpp   # exported keyPositions() for write notifications
`-- tests/test_v10_1.py
```

## File Responsibilities

- `multi.cpp` — `watched_keys` map; `WATCH`, dirty marking on writes, `EXEC` abort via `*-1`.
- `commands.cpp` — `notifyWriteKeys()` after successful writes.
- `eventloop.cpp` — `watchCleanup()` on disconnect.
