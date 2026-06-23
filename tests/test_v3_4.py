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
SERVER_BIN = ROOT / "tests" / "server_v3_4_bin"
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


def read_bulk_array(reader, count):
    items = []
    for _ in range(count):
        line = read_line(reader)
        assert line.startswith(b"$"), line
        length = int(line[1:-2])
        data = reader.read(length + 2)
        assert data.endswith(b"\r\n")
        items.append(data[:-2].decode("utf-8"))
    return items


def test_set_algebra_from_guide():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SADD", "s1", "a", "b", "c", "d"))
        assert read_line(reader) == b":4\r\n"

        sock.sendall(command("SADD", "s2", "c", "d", "e", "f"))
        assert read_line(reader) == b":4\r\n"

        sock.sendall(command("SCARD", "s1"))
        assert read_line(reader) == b":4\r\n"

        sock.sendall(command("SINTER", "s1", "s2"))
        assert read_line(reader) == b"*2\r\n"
        assert sorted(read_bulk_array(reader, 2)) == ["c", "d"]

        sock.sendall(command("SUNION", "s1", "s2"))
        assert read_line(reader) == b"*6\r\n"
        assert sorted(read_bulk_array(reader, 6)) == ["a", "b", "c", "d", "e", "f"]

        sock.sendall(command("SDIFF", "s1", "s2"))
        assert read_line(reader) == b"*2\r\n"
        assert sorted(read_bulk_array(reader, 2)) == ["a", "b"]


def test_sismember_and_srem():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SISMEMBER", "s1", "a"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("SMISMEMBER", "s1", "a", "z"))
        assert read_line(reader) == b"*2\r\n"
        assert read_line(reader) == b":1\r\n"
        assert read_line(reader) == b":0\r\n"

        sock.sendall(command("SREM", "s1", "a", "missing"))
        assert read_line(reader) == b":1\r\n"


def test_store_commands():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SINTERSTORE", "out", "s1", "s2"))
        assert read_line(reader) == b":2\r\n"

        sock.sendall(command("SMEMBERS", "out"))
        assert read_line(reader) == b"*2\r\n"
        assert sorted(read_bulk_array(reader, 2)) == ["c", "d"]


def run_tests():
    compile_server()
    proc = start_server()
    try:
        test_set_algebra_from_guide()
        test_sismember_and_srem()
        test_store_commands()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v3.4 set command tests passed")
