import os
import socket
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER_SRC = ROOT / "server.cpp"
PARSER_SRC = ROOT / "parser.cpp"
RESP_SRC = ROOT / "resp.cpp"
OBJECT_SRC = ROOT / "object.cpp"
CMD_STRING_SRC = ROOT / "cmd_string.cpp"
CMD_HASH_SRC = ROOT / "cmd_hash.cpp"
CMD_LIST_SRC = ROOT / "cmd_list.cpp"
CMD_SET_SRC = ROOT / "cmd_set.cpp"
EVENTLOOP_SRC = ROOT / "eventloop.cpp"
SERVER_BIN = ROOT / "tests" / "server_v0_2_bin"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    cxx = os.environ.get("CXX", "g++")
    subprocess.run(
        [
            cxx,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-pthread",
            "-o",
            str(SERVER_BIN),
            str(SERVER_SRC),
            str(PARSER_SRC),
            str(RESP_SRC),
            str(OBJECT_SRC),
            str(CMD_STRING_SRC),
            str(CMD_HASH_SRC),
            str(CMD_LIST_SRC),
            str(CMD_SET_SRC),
            str(EVENTLOOP_SRC),
        ],
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


def test_tokenizer_and_dispatch():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        reader = sock.makefile("rb")

        sock.sendall(b'set mykey "hello world"\nGET mykey\n')
        assert read_line(reader) == b"+OK\r\n"
        assert reader.read(18) == b"$11\r\nhello world\r\n"

        sock.sendall(b'SET quote "hello \\"redis\\""\nGET quote\n')
        assert read_line(reader) == b"+OK\r\n"
        assert reader.read(20) == b'$13\r\nhello "redis"\r\n'

        sock.sendall(b"GET mykey extra\n")
        assert read_line(reader) == b"-ERR wrong number of arguments for 'GET' command\r\n"

        sock.sendall(b'SET broken "unterminated\n')
        assert read_line(reader) == b"-ERR unterminated quoted string\r\n"


def run_tests():
    compile_server()

    proc = start_server()
    try:
        test_tokenizer_and_dispatch()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v0.2 tokenizer tests passed")
