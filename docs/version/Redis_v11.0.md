# Redis_v11.0 Documentation

# Goal

Confirm that pipelined RESP commands are handled correctly: multiple commands in one `send()` produce multiple replies without per-command flushes mid-parse.

# Previous limitation

Not explicitly tested; needed verification that the event loop accumulates replies in `write_buf` across the `tryParse` loop.

# Concepts

- **Pipelining** — client sends many commands before reading any reply; server parses all complete frames from the read buffer and appends each reply to `write_buf`, then flushes once when the socket is writable.

# Design

No code changes required. `eventloop.cpp` already calls `queueParsedReplies()` inside a `while (client.parser.tryParse(argv))` loop, appending each dispatch result to `client.write_buf`.

# Files

- `tests/test_v11_0.py` — sends 10 `PING` commands in one batch, asserts 10 replies.

# Testing

```bash
python tests/test_v11_0.py
```

Result: passed (WSL).

# Next

V11.1 — Lua scripting (`EVAL`, `EVALSHA`, `SCRIPT`).
