from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# V6+ requires commands.cpp and the full module graph for every server/probe link.
CORE_SOURCES = [
    ROOT / "parser.cpp",
    ROOT / "commands.cpp",
    ROOT / "resp.cpp",
    ROOT / "sds.cpp",
    ROOT / "listpack.cpp",
    ROOT / "intset.cpp",
    ROOT / "encoding.cpp",
    ROOT / "object.cpp",
    ROOT / "cmd_string.cpp",
    ROOT / "cmd_expire.cpp",
    ROOT / "cmd_hash.cpp",
    ROOT / "cmd_list.cpp",
    ROOT / "cmd_set.cpp",
    ROOT / "cmd_zset.cpp",
    ROOT / "skiplist.cpp",
    ROOT / "eviction.cpp",
    ROOT / "rdb.cpp",
]

SERVER_SOURCES = CORE_SOURCES + [
    ROOT / "server.cpp",
    ROOT / "eventloop.cpp",
]
