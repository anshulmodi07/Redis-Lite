import re
from pathlib import Path

TESTS = Path(__file__).parent

for path in sorted(TESTS.glob("test_v*.py")):
    text = path.read_text(encoding="utf-8")
    if "from build_sources import" not in text:
        continue

    if "COMPILE_FLAGS" not in text:
        text = re.sub(
            r"from build_sources import ([^\n]+)",
            lambda match: (
                "from build_sources import COMPILE_FLAGS, " + match.group(1)
                if "COMPILE_FLAGS" not in match.group(1)
                else match.group(0)
            ),
            text,
            count=1,
        )

    if '*map(str, COMPILE_FLAGS)' not in text:
        text = text.replace(
            '[cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", *map(str, EXTRA_LIBS), "-o",',
            '[cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", *map(str, COMPILE_FLAGS), *map(str, EXTRA_LIBS), "-o",',
        )
        text = text.replace(
            '[cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", "-o",',
            '[cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", *map(str, COMPILE_FLAGS), "-o",',
        )

    path.write_text(text, encoding="utf-8")
    print("patched", path.name)
