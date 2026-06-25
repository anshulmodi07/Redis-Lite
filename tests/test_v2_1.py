import os
import socket
import subprocess
import time
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary, COMPILE_FLAGS, CORE_SOURCES, SERVER_SOURCES


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
SERVER_BIN = ROOT / "tests" / "server_v2_1_bin"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    compile_binary(SERVER_BIN)


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


def test_epoll_handles_idle_and_active_clients():
    idle = socket.create_connection((HOST, PORT), timeout=2)
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as active:
            active.settimeout(1)
            reader = active.makefile("rb")

            active.sendall(command("SET", "epoll", "ready"))
            assert reader.readline() == b"+OK\r\n"

            active.sendall(command("GET", "epoll"))
            assert reader.read(11) == b"$5\r\nready\r\n"
    finally:
        idle.close()


def test_epoll_keeps_pipelined_replies_ordered():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(
            command("PING")
            + command("SET", "a", "1")
            + command("GET", "a")
            + command("GET", "missing")
        )

        assert reader.readline() == b"+PONG\r\n"
        assert reader.readline() == b"+OK\r\n"
        assert reader.read(7) == b"$1\r\n1\r\n"
        assert reader.readline() == b"$-1\r\n"


def run_tests():
    compile_server()

    proc = start_server()
    try:
        test_epoll_handles_idle_and_active_clients()
        test_epoll_keeps_pipelined_replies_ordered()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v2.1 epoll event loop tests passed")
