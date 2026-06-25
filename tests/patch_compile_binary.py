from pathlib import Path
import re

TESTS = Path(__file__).parent

COMPILE_FN = '''def compile_server():
    compile_binary(SERVER_BIN)
'''

for path in sorted(TESTS.glob("test_v*.py")):
    text = path.read_text(encoding="utf-8")
    if "from build_sources import" not in text:
        continue

    if "compile_binary" not in text:
        text = re.sub(
            r"from build_sources import ([^\n]+)",
            lambda match: (
                "from build_sources import compile_binary, " + match.group(1)
                if "compile_binary" not in match.group(1)
                else match.group(0)
            ),
            text,
            count=1,
        )

    if "def compile_server(out_path" in text:
        text = re.sub(
            r"def compile_server\(out_path\):[\s\S]*?check=True,\s*\)",
            "def compile_server(out_path):\n    compile_binary(out_path)",
            text,
            count=1,
        )
    elif re.search(r"def compile_server\(\):[\s\S]*?check=True,\s*\)", text):
        text = re.sub(
            r"def compile_server\(\):[\s\S]*?check=True,\s*\)",
            COMPILE_FN.strip(),
            text,
            count=1,
        )

    path.write_text(text, encoding="utf-8")
    print("patched", path.name)
