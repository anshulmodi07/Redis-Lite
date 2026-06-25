import os
import subprocess
import tempfile
import textwrap
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary, COMPILE_FLAGS, CORE_SOURCES, SERVER_SOURCES


ROOT = Path(__file__).resolve().parents[1]


def run_probe():
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "probe.cpp"
        out = Path(tmp) / "probe"
        src.write_text(textwrap.dedent("""
            #include "db.h"
            #include <cassert>

            int main() {
                RedisDb db;
                db.data["old"] = createStringObject("gone");
                db.data["live"] = createStringObject("here");
                db.expires["old"] = nowMs() - 1;
                db.expires["live"] = nowMs() + 100000;

                assert(activeExpireCycle(db) == 1);
                assert(db.data.count("old") == 0);
                assert(db.expires.count("old") == 0);
                assert(db.data.count("live") == 1);
                assert(db.expires.count("live") == 1);

                destroyObject(db.data["live"]);
            }
        """))
        subprocess.run(
            [cxx, "-std=c++17", "-Wall", "-Wextra", "-I", str(ROOT), str(src), str(ROOT / "sds.cpp"), str(ROOT / "object.cpp"), str(ROOT / "skiplist.cpp"), "-o", str(out)],
            check=True,
        )
        subprocess.run([str(out)], check=True)


if __name__ == "__main__":
    run_probe()
    print("v4.2 active expiry tests passed")
