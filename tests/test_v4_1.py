import os
import subprocess
import tempfile
import textwrap
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary, COMPILE_FLAGS, CORE_SOURCES, SERVER_SOURCES, LUA_SOURCES


ROOT = Path(__file__).resolve().parents[1]
SOURCES = CORE_SOURCES


def run_probe():
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "probe.cpp"
        out = Path(tmp) / "probe"
        src.write_text(textwrap.dedent("""
            #include "dispatch_probe.h"
            #include <cassert>
            #include <vector>

            int main() {
                std::vector<RedisDb> dbs(1);
                RedisDb& db = dbs[0];
                assert(dispatchProbe(dbs, {"SET", "live", "ok"}) == "+OK\\r\\n");
                db.expires["live"] = nowMs() - 1;
                assert(dispatchProbe(dbs, {"GET", "live"}) == "$-1\\r\\n");
                assert(db.data.count("live") == 0);
                assert(db.expires.count("live") == 0);

                assert(dispatchProbe(dbs, {"SADD", "s", "a"}) == ":1\\r\\n");
                db.expires["s"] = nowMs() - 1;
                assert(dispatchProbe(dbs, {"SCARD", "s"}) == ":0\\r\\n");
            }
        """))
        compile_binary(
            out,
            sources=[src] + SOURCES + LUA_SOURCES,
            extra_args=["-I", str(ROOT), "-I", str(ROOT / "tests")]
        )
        subprocess.run([str(out)], check=True)


if __name__ == "__main__":
    run_probe()
    print("v4.1 lazy expiry tests passed")
