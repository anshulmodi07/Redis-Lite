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
SERVER_BIN = ROOT / "tests" / "server_v3_5_bin"
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
    frame = f"*{len(parts)}\r\n".encode()
    for part in parts:
        data = part.encode()
        frame += f"${len(data)}\r\n".encode() + data + b"\r\n"
    return frame


def read_line(reader):
    line = reader.readline()
    assert line.endswith(b"\r\n"), line
    return line


def read_bulk(reader):
    line = read_line(reader)
    assert line.startswith(b"$"), line
    length = int(line[1:-2])
    if length < 0:
        return None
    data = reader.read(length + 2)
    assert data.endswith(b"\r\n")
    return data[:-2].decode()


def read_array(reader):
    line = read_line(reader)
    assert line.startswith(b"*"), line
    count = int(line[1:-2])
    return [read_bulk(reader) for _ in range(count)]


def test_leaderboard_flow():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("ZADD", "leaderboard", "1500", "alice", "2300", "bob", "1800", "carol", "2100", "dave"))
        assert read_line(reader) == b":4\r\n"

        sock.sendall(command("ZRANGE", "leaderboard", "0", "-1", "WITHSCORES"))
        assert read_array(reader) == ["alice", "1500", "carol", "1800", "dave", "2100", "bob", "2300"]

        sock.sendall(command("ZREVRANGE", "leaderboard", "0", "2"))
        assert read_array(reader) == ["bob", "dave", "carol"]

        sock.sendall(command("ZRANK", "leaderboard", "alice"))
        assert read_line(reader) == b":0\r\n"

        sock.sendall(command("ZINCRBY", "leaderboard", "1000", "alice"))
        assert read_bulk(reader) == "2500"

        sock.sendall(command("ZRANK", "leaderboard", "alice"))
        assert read_line(reader) == b":3\r\n"

        sock.sendall(command("ZRANGEBYSCORE", "leaderboard", "1800", "2200"))
        assert read_array(reader) == ["carol", "dave"]


def test_updates_options_and_pop():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("ZADD", "z", "1", "a", "2", "b"))
        assert read_line(reader) == b":2\r\n"

        sock.sendall(command("ZADD", "z", "NX", "3", "a", "4", "c"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("ZADD", "z", "XX", "CH", "5", "a", "6", "missing"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("ZSCORE", "z", "a"))
        assert read_bulk(reader) == "5"

        sock.sendall(command("ZCOUNT", "z", "-inf", "+inf"))
        assert read_line(reader) == b":3\r\n"

        sock.sendall(command("ZPOPMIN", "z", "2"))
        assert read_array(reader) == ["b", "2", "c", "4"]

        sock.sendall(command("ZREM", "z", "a", "none"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("ZCARD", "z"))
        assert read_line(reader) == b":0\r\n"


def run_tests():
    compile_server()
    proc = start_server()
    try:
        test_leaderboard_flow()
        test_updates_options_and_pop()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v3.5 sorted set command tests passed")
