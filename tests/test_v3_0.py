import os
import socket
import subprocess
import time
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent))
from build_sources import CORE_SOURCES, SERVER_SOURCES


ROOT = Path(__file__).resolve().parents[1]
SERVER_SRC = ROOT / "server.cpp"
PARSER_SRC = ROOT / "parser.cpp"
RESP_SRC = ROOT / "resp.cpp"
SDS_SRC = ROOT / "sds.cpp"
OBJECT_SRC = ROOT / "object.cpp"
CMD_STRING_SRC = ROOT / "cmd_string.cpp"
CMD_HASH_SRC = ROOT / "cmd_hash.cpp"
CMD_LIST_SRC = ROOT / "cmd_list.cpp"
CMD_SET_SRC = ROOT / "cmd_set.cpp"
CMD_ZSET_SRC = ROOT / "cmd_zset.cpp"
SKIPLIST_SRC = ROOT / "skiplist.cpp"
EVENTLOOP_SRC = ROOT / "eventloop.cpp"
SERVER_BIN = ROOT / "tests" / "server_v3_0_bin"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    cxx = os.environ.get("CXX", "g++")
    subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", "-o", str(SERVER_BIN), *map(str, SERVER_SOURCES)],
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


def test_type_reports_string_and_none():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SET", "foo", "bar"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("TYPE", "foo"))
        assert read_line(reader) == b"+string\r\n"

        sock.sendall(command("TYPE", "missing"))
        assert read_line(reader) == b"+none\r\n"


def test_del_frees_keys_and_exists_counts():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SET", "foo", "bar"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("EXISTS", "foo", "missing"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("DEL", "foo"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("EXISTS", "foo"))
        assert read_line(reader) == b":0\r\n"

        sock.sendall(command("GET", "foo"))
        assert read_line(reader) == b"$-1\r\n"


def test_set_replaces_existing_object():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SET", "key", "first"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("SET", "key", "second"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("GET", "key"))
        assert reader.read(12) == b"$6\r\nsecond\r\n"


def run_tests():
    compile_server()

    proc = start_server()
    try:
        test_type_reports_string_and_none()
        test_del_frees_keys_and_exists_counts()
        test_set_replaces_existing_object()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v3.0 typed object tests passed")
