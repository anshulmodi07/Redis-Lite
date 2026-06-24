# Project Structure

Current version: V10.0

```text
|-- multi.h / multi.cpp     # MULTI/EXEC/DISCARD transaction queue
|-- client.h                # in_multi, multi_error, queued_commands
|-- resp.h / resp.cpp       # encodeRespArray for EXEC replies
`-- tests/test_v10_0.py
```

## File Responsibilities

- `multi.cpp` — queue commands after `MULTI`, run batch on `EXEC`, `DISCARD` clears state.
- `commands.cpp` — `tryTransaction()` before normal dispatch; `exec_replay` skips re-queueing.
- `client.h` — per-connection transaction state.
