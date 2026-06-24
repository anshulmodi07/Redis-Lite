import re
import sys
from pathlib import Path

TESTS = Path(__file__).parent

IMPORT_BLOCK = """import sys
sys.path.insert(0, str(Path(__file__).parent))
from build_sources import CORE_SOURCES, SERVER_SOURCES

"""

for path in sorted(TESTS.glob("test_v*.py")):
    text = path.read_text(encoding="utf-8")
    if "from build_sources import" in text and path.name != "test_v6_0.py":
        print("skip", path.name)
        continue

    if path.name == "test_v6_0.py":
        if "eviction.cpp" not in text:
            text = text.replace(
                'ROOT / "skiplist.cpp",\n    ROOT / "server.cpp",',
                'ROOT / "skiplist.cpp",\n    ROOT / "eviction.cpp",\n    ROOT / "server.cpp",',
            )
            path.write_text(text, encoding="utf-8")
            print("patched", path.name)
        continue

    if "from pathlib import Path" not in text:
        continue

    if "from build_sources import" not in text:
        text = text.replace(
            "from pathlib import Path\n\n",
            "from pathlib import Path\n\n" + IMPORT_BLOCK,
            1,
        )

    text = re.sub(
        r"PROBE_SOURCES = \[[\s\S]*?\]\nSERVER_SOURCES = PROBE_SOURCES \+ \[[\s\S]*?\]\n",
        "PROBE_SOURCES = CORE_SOURCES\n",
        text,
        count=1,
    )
    text = re.sub(
        r"^SOURCES = \[[\s\S]*?\]\n",
        "SOURCES = CORE_SOURCES\n",
        text,
        count=1,
        flags=re.M,
    )

    if "PARSER_SRC = ROOT" in text:
        text = re.sub(
            r"def compile_server\(\):[\s\S]*?check=True,\s*\)",
            """def compile_server():
    cxx = os.environ.get("CXX", "g++")
    subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", "-o", str(SERVER_BIN), *map(str, SERVER_SOURCES)],
        cwd=ROOT,
        check=True,
    )""",
            text,
            count=1,
        )

    path.write_text(text, encoding="utf-8")
    print("patched", path.name)
