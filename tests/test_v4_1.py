import os
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    ROOT / "parser.cpp",
    ROOT / "resp.cpp",
    ROOT / "object.cpp",
    ROOT / "cmd_string.cpp",
    ROOT / "cmd_hash.cpp",
    ROOT / "cmd_list.cpp",
    ROOT / "cmd_set.cpp",
    ROOT / "cmd_zset.cpp",
    ROOT / "skiplist.cpp",
]


def run_probe():
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "probe.cpp"
        out = Path(tmp) / "probe"
        src.write_text(textwrap.dedent("""
            #include "parser.h"
            #include <cassert>

            int main() {
                RedisDb db;
                assert(dispatch({"SET", "live", "ok"}, db) == "+OK\\r\\n");
                db.expires["live"] = nowMs() - 1;
                assert(dispatch({"GET", "live"}, db) == "$-1\\r\\n");
                assert(db.data.count("live") == 0);
                assert(db.expires.count("live") == 0);

                assert(dispatch({"SADD", "s", "a"}, db) == ":1\\r\\n");
                db.expires["s"] = nowMs() - 1;
                assert(dispatch({"SCARD", "s"}, db) == ":0\\r\\n");
            }
        """))
        subprocess.run(
            [cxx, "-std=c++17", "-Wall", "-Wextra", "-I", str(ROOT), str(src), *map(str, SOURCES), "-o", str(out)],
            check=True,
        )
        subprocess.run([str(out)], check=True)


if __name__ == "__main__":
    run_probe()
    print("v4.1 lazy expiry tests passed")
