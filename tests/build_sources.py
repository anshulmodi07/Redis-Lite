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
def _jemalloc_link_flags():
    if os.environ.get("REDIS_LITE_USE_JEMALLOC", "1") == "0":
        return []

    for pkg in ("jemalloc", "libjemalloc"):
        try:
            result = subprocess.run(
                ["pkg-config", "--libs", pkg],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode == 0 and result.stdout.strip():
                return result.stdout.strip().split()
        except FileNotFoundError:
            pass

    for candidate in (
        "/usr/lib/x86_64-linux-gnu/libjemalloc.so",
        "/usr/lib64/libjemalloc.so",
        "/usr/local/lib/libjemalloc.so",
        "/opt/homebrew/lib/libjemalloc.dylib",
    ):
        if os.path.exists(candidate):
            lib_dir = os.path.dirname(candidate)
            return [f"-L{lib_dir}", "-ljemalloc"]

    return []


EXTRA_LIBS = _jemalloc_link_flags()


def _lua_object_path(source: Path) -> Path:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    return BUILD_DIR / (source.name + ".o")

LUA_OBJECTS = [_lua_object_path(source) for source in LUA_SOURCES]


def compile_binary(out_path, sources=None, extra_args=None):
    out_path = Path(out_path)
    sources = sources or SERVER_SOURCES
    
    # Check if target is already up-to-date to avoid expensive recompilation
    if out_path.exists():
        max_mtime = 0.0
        for source in sources:
            if source.exists():
                max_mtime = max(max_mtime, source.stat().st_mtime)
        for header in ROOT.glob("*.h"):
            max_mtime = max(max_mtime, header.stat().st_mtime)
            
        if out_path.stat().st_mtime > max_mtime:
            # Already compiled and up-to-date!
            return

    # If it is the default build, compile to a shared binary first
    if sources == SERVER_SOURCES and not extra_args:
        shared_bin = BUILD_DIR / "server_shared_bin"
        shared_up_to_date = False
        if shared_bin.exists():
            max_mtime = 0.0
            for source in sources:
                if source.exists():
                    max_mtime = max(max_mtime, source.stat().st_mtime)
            for header in ROOT.glob("*.h"):
                max_mtime = max(max_mtime, header.stat().st_mtime)
            if shared_bin.stat().st_mtime > max_mtime:
                shared_up_to_date = True
        
        if not shared_up_to_date:
            print(f"Compiling shared binary: {shared_bin}...", flush=True)
            _compile_to_path(shared_bin, sources, extra_args)
            
        import shutil
        shutil.copy2(shared_bin, out_path)
        return

    _compile_to_path(out_path, sources, extra_args)

def _compile_to_path(out_path, sources, extra_args):
    cxx = os.environ.get("CXX", "g++")
    cc = os.environ.get("CC", "gcc")
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

