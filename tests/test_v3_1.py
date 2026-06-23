import os
import socket
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    ROOT / "server.cpp",
    ROOT / "parser.cpp",
    ROOT / "resp.cpp",
    ROOT / "object.cpp",
    ROOT / "cmd_string.cpp",
    ROOT / "cmd_hash.cpp",
    ROOT / "cmd_list.cpp",
    ROOT / "cmd_set.cpp",
    ROOT / "cmd_zset.cpp",
    ROOT / "skiplist.cpp",
    ROOT / "eventloop.cpp",
]
SERVER_BIN = ROOT / "tests" / "server_v3_1_bin"
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


def test_incr_and_append():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SET", "counter", "0"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("INCR", "counter"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("INCRBY", "counter", "5"))
        assert read_line(reader) == b":6\r\n"

        sock.sendall(command("APPEND", "mykey", "hello"))
        assert read_line(reader) == b":5\r\n"

        sock.sendall(command("APPEND", "mykey", " world"))
        assert read_line(reader) == b":11\r\n"

        sock.sendall(command("STRLEN", "mykey"))
        assert read_line(reader) == b":11\r\n"


def test_setnx_and_set_nx():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SETNX", "mykey", "999"))
        assert read_line(reader) == b":0\r\n"

        sock.sendall(command("SET", "k", "v", "NX"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("SET", "k", "v2", "NX"))
        assert read_line(reader) == b"$-1\r\n"


def test_mset_mget_and_getset():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("MSET", "a", "1", "b", "2"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("MGET", "a", "b", "missing"))
        assert read_line(reader) == b"*3\r\n"
        assert reader.read(7) == b"$1\r\n1\r\n"
        assert reader.read(7) == b"$1\r\n2\r\n"
        assert read_line(reader) == b"$-1\r\n"

        sock.sendall(command("GETSET", "a", "9"))
        assert reader.read(7) == b"$1\r\n1\r\n"
        sock.sendall(command("GET", "a"))
        assert reader.read(7) == b"$1\r\n9\r\n"


def run_tests():
    compile_server()
    proc = start_server()
    try:
        test_incr_and_append()
        test_setnx_and_set_nx()
        test_mset_mget_and_getset()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v3.1 string command tests passed")
