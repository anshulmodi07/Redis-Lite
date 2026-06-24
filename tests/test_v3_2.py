import os
import socket
import subprocess
import time
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent))
from build_sources import CORE_SOURCES, SERVER_SOURCES


ROOT = Path(__file__).resolve().parents[1]
SOURCES = CORE_SOURCES
SERVER_BIN = ROOT / "tests" / "server_v3_2_bin"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    cxx = os.environ.get("CXX", "g++")
    subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", "-o", str(SERVER_BIN), *map(str, SOURCES)],
        cwd=ROOT,
        check=True,
    )


def start_server():
    proc = subprocess.Popen(
        [str(SERVER_BIN)],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
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
    frame = f"*{len(parts)}\r\n".encode("utf-8")
    for part in parts:
        data = part.encode("utf-8")
        frame += f"${len(data)}\r\n".encode("utf-8") + data + b"\r\n"
    return frame


def read_line(reader):
    line = reader.readline()
    assert line.endswith(b"\r\n"), line
    return line


def read_bulk(reader, length):
    data = reader.read(length + 2)
    assert data.endswith(b"\r\n"), data
    return data[:-2]


def test_hset_hgetall_and_hincrby():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("HSET", "user:1", "name", "Alice", "age", "30", "city", "Delhi"))
        assert read_line(reader) == b":3\r\n"

        sock.sendall(command("HGET", "user:1", "name"))
        assert reader.read(11) == b"$5\r\nAlice\r\n"

        sock.sendall(command("HINCRBY", "user:1", "age", "1"))
        assert read_line(reader) == b":31\r\n"

        sock.sendall(command("HGET", "user:1", "age"))
        assert reader.read(8) == b"$2\r\n31\r\n"

        sock.sendall(command("HLEN", "user:1"))
        assert read_line(reader) == b":3\r\n"


def test_hmget_hdel_hexists():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("HMGET", "user:1", "name", "missing"))
        assert read_line(reader) == b"*2\r\n"
        assert reader.read(11) == b"$5\r\nAlice\r\n"
        assert read_line(reader) == b"$-1\r\n"

        sock.sendall(command("HEXISTS", "user:1", "city"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("HDEL", "user:1", "city"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("HEXISTS", "user:1", "city"))
        assert read_line(reader) == b":0\r\n"


def test_wrongtype_after_string_overwrite():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SET", "user:1", "oops"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("HGET", "user:1", "name"))
        assert read_line(reader).startswith(b"-WRONGTYPE")


def run_tests():
    compile_server()
    proc = start_server()
    try:
        test_hset_hgetall_and_hincrby()
        test_hmget_hdel_hexists()
        test_wrongtype_after_string_overwrite()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v3.2 hash command tests passed")
