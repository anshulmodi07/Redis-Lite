import os
import socket
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROBE_SOURCES = [
    ROOT / "parser.cpp",
    ROOT / "resp.cpp",
    ROOT / "sds.cpp",
    ROOT / "listpack.cpp",
    ROOT / "object.cpp",
    ROOT / "cmd_string.cpp",
    ROOT / "cmd_expire.cpp",
    ROOT / "cmd_hash.cpp",
    ROOT / "cmd_list.cpp",
    ROOT / "cmd_set.cpp",
    ROOT / "cmd_zset.cpp",
    ROOT / "skiplist.cpp",
]
SERVER_SOURCES = PROBE_SOURCES + [
    ROOT / "server.cpp",
    ROOT / "eventloop.cpp",
]
SERVER_BIN = ROOT / "tests" / "server_v5_1_bin"
PROBE_BIN = ROOT / "tests" / "probe_v5_1_bin"
PROBE_SRC = ROOT / "tests" / "probe_v5_1.cpp"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    cxx = os.environ.get("CXX", "g++")
    subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", "-o", str(SERVER_BIN), *map(str, SERVER_SOURCES)],
        cwd=ROOT,
        check=True,
    )


def run_probe():
    cxx = os.environ.get("CXX", "g++")
    subprocess.run(
        [
            cxx,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-I",
            str(ROOT),
            str(PROBE_SRC),
            ROOT / "listpack.cpp",
            "-o",
            str(PROBE_BIN),
        ],
        check=True,
    )
    subprocess.run([str(PROBE_BIN)], check=True)


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


def test_regression():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("HSET", "user:1", "name", "Alice", "age", "30"))
        assert read_line(reader) == b":2\r\n"

        sock.sendall(command("RPUSH", "mylist", "a", "b", "c"))
        assert read_line(reader) == b":3\r\n"

        sock.sendall(command("ZADD", "scores", "10", "alice", "20", "bob"))
        assert read_line(reader) == b":2\r\n"


def main():
    run_probe()
    compile_server()
    proc = start_server()
    try:
        test_regression()
    finally:
        stop_server(proc)
    print("test_v5_1.py: all tests passed")


if __name__ == "__main__":
    main()
