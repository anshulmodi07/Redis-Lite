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
SERVER_BIN = ROOT / "tests" / "server_v1_0_bin"
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


def read_line(reader):
    return reader.readline()


def command(*parts):
    frame = f"*{len(parts)}\r\n".encode("utf-8")
    for part in parts:
        data = part.encode("utf-8")
        frame += f"${len(data)}\r\n".encode("utf-8") + data + b"\r\n"
    return frame


def test_resp_commands_and_pipelining():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        reader = sock.makefile("rb")

        sock.sendall(command("ping"))
        assert read_line(reader) == b"+PONG\r\n"

        sock.sendall(command("SET", "space", "hello world") + command("GET", "space"))
        assert read_line(reader) == b"+OK\r\n"
        assert reader.read(18) == b"$11\r\nhello world\r\n"


def test_partial_bulk_string():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        reader = sock.makefile("rb")

        payload = command("SET", "split", "abcde")
        sock.sendall(payload[:15])
        time.sleep(0.05)
        sock.sendall(payload[15:])
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("GET", "split"))
        assert reader.read(11) == b"$5\r\nabcde\r\n"


def test_malformed_resp_errors():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        reader = sock.makefile("rb")
        sock.sendall(b"*1\r\n+PING\r\n")
        assert read_line(reader) == b"-ERR protocol error: expected bulk string\r\n"


def run_tests():
    compile_server()

    proc = start_server()
    try:
        test_resp_commands_and_pipelining()
        test_partial_bulk_string()
        test_malformed_resp_errors()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v1.0 resp decoder tests passed")
