# Project Structure

Current version: V3.3

```text
|-- cmd_list.cpp        # List command handlers (LPUSH, LRANGE, LPOP, ...)
|-- cmd_list.h
|-- cmd_hash.cpp / cmd_string.cpp / object.cpp
|-- parser.cpp          # Routes list commands to cmd_list
`-- tests/test_v3_3.py
```

## File Responsibilities

- `cmd_list.h` / `cmd_list.cpp` — list commands on `std::list<string>` inside `OBJ_LIST`; `lookupList()`, index normalization, `dispatchListCommand()`.
- `object.cpp` — `createListObject()` allocates the backing list.
