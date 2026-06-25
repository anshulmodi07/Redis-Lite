import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LUA_ROOT = ROOT / "third_party" / "lua-5.1.5" / "src"
BUILD_DIR = ROOT / "tests" / ".build"

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
    ROOT / "aof.cpp",
    ROOT / "pubsub.cpp",
    ROOT / "multi.cpp",
    ROOT / "sha1.cpp",
    ROOT / "scripting.cpp",
    ROOT / "replication.cpp",
    ROOT / "cluster.cpp",
    ROOT / "server_config.cpp",
]

LUA_SOURCES = [
    LUA_ROOT / name
    for name in (
        "lapi.c",
        "lcode.c",
        "ldebug.c",
        "ldo.c",
        "ldump.c",
        "lfunc.c",
        "lgc.c",
        "llex.c",
        "lmem.c",
        "lobject.c",
        "lopcodes.c",
        "lparser.c",
        "lstate.c",
        "lstring.c",
        "ltable.c",
        "ltm.c",
        "lundump.c",
        "lvm.c",
        "lzio.c",
        "lauxlib.c",
        "lbaselib.c",
        "ldblib.c",
        "liolib.c",
        "lmathlib.c",
        "loslib.c",
        "ltablib.c",
        "lstrlib.c",
        "loadlib.c",
        "linit.c",
    )
]

SERVER_SOURCES = CORE_SOURCES + LUA_SOURCES + [
    ROOT / "server.cpp",
    ROOT / "eventloop.cpp",
]

COMPILE_FLAGS = ["-I", str(LUA_ROOT)]
EXTRA_LIBS = []


def _lua_object_path(source: Path) -> Path:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    return BUILD_DIR / (source.name + ".o")


def compile_binary(out_path, sources=None, extra_args=None):
    cxx = os.environ.get("CXX", "g++")
    cc = os.environ.get("CC", "gcc")
    sources = sources or SERVER_SOURCES
    extra = extra_args or []

    cxx_sources = []
    lua_objects = []
    for source in sources:
        if source.suffix == ".c":
            obj = _lua_object_path(source)
            if not obj.exists() or obj.stat().st_mtime < source.stat().st_mtime:
                subprocess.run(
                    [
                        cc,
                        "-std=c99",
                        "-Wall",
                        "-Wextra",
                        "-x",
                        "c",
                        *map(str, COMPILE_FLAGS),
                        "-c",
                        str(source),
                        "-o",
                        str(obj),
                    ],
                    cwd=ROOT,
                    check=True,
                )
            lua_objects.append(obj)
        else:
            cxx_sources.append(source)

    subprocess.run(
        [
            cxx,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-pthread",
            *map(str, COMPILE_FLAGS),
            *map(str, EXTRA_LIBS),
            *map(str, extra),
            "-o",
            str(out_path),
            *map(str, cxx_sources),
            *map(str, lua_objects),
        ],
        cwd=ROOT,
        check=True,
    )
