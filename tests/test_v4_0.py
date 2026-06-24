import os
import socket
import subprocess
import tempfile
import textwrap
import time
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent))
from build_sources import CORE_SOURCES, SERVER_SOURCES


ROOT = Path(__file__).resolve().parents[1]
SOURCES = CORE_SOURCES
SERVER_BIN = ROOT / "tests" / "server_v4_0_bin"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    cxx = os.environ.get("CXX", "g++")
    subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", "-o", str(SERVER_BIN), *map(str, SOURCES)],
        cwd=ROOT,
        check=True,
    )


def compile_metadata_probe():
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "probe.cpp"
        out = Path(tmp) / "probe"
        src.write_text(textwrap.dedent("""
            #include "db.h"
            #include <cassert>
            #include <thread>

            int main() {
                RedisDb db;
                db.expires["k"] = nowMs() + 1000;
                assert(db.expires.count("k") == 1);
                long long first = nowMs();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                assert(nowMs() >= first);
            }
        """))
        subprocess.run([cxx, "-std=c++17", "-I", str(ROOT), str(src), "-o", str(out)], check=True)
        subprocess.run([str(out)], check=True)


def start_server():
    proc = subprocess.Popen([str(SERVER_BIN)], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            with socket.create_connection((HOST, PORT), timeout=0.2):
                return proc
        except OSError:
            if proc.poll() is not None:
                raise RuntimeError("server exited before accepting connections")
            time.sleep(0.05)
    stop_server(proc)
    raise RuntimeError("server did not start listening in time")


def stop_server(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)


def command(*parts):
    frame = f"*{len(parts)}\r\n".encode()
    for part in parts:
        data = part.encode()
        frame += f"${len(data)}\r\n".encode() + data + b"\r\n"
    return frame


def test_existing_commands_still_work():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        reader = sock.makefile("rb")
        sock.sendall(command("SET", "v4", "ok"))
        assert reader.readline() == b"+OK\r\n"
        sock.sendall(command("GET", "v4"))
        assert reader.read(8) == b"$2\r\nok\r\n"


def run_tests():
    compile_metadata_probe()
    compile_server()
    proc = start_server()
    try:
        test_existing_commands_still_work()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v4.0 expiry metadata tests passed")
