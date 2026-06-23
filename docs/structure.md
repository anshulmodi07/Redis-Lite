# Project Structure

Current version: V3.4

```text
|-- cmd_set.cpp         # Set command handlers (SADD, SINTER, SPOP, ...)
|-- cmd_set.h
|-- cmd_list.cpp / cmd_hash.cpp / cmd_string.cpp / object.cpp
|-- parser.cpp
`-- tests/test_v3_4.py
```

## File Responsibilities

- `cmd_set.h` / `cmd_set.cpp` — set commands on `unordered_set<string>` inside `OBJ_SET`; set algebra and `dispatchSetCommand()`.
- `object.cpp` — `createSetObject()` allocates the backing set.
