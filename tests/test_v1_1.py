import os
import socket
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER_SRC = ROOT / "server.cpp"
PARSER_SRC = ROOT / "parser.cpp"
RESP_SRC = ROOT / "resp.cpp"
SERVER_BIN = ROOT / "tests" / "server_v1_1_bin"
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


def command(*parts):
    frame = f"*{len(parts)}\r\n".encode("utf-8")
    for part in parts:
        data = part.encode("utf-8")
        frame += f"${len(data)}\r\n".encode("utf-8") + data + b"\r\n"
    return frame


def test_resp_encoded_replies():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        reader = sock.makefile("rb")

        sock.sendall(command("PING"))
        assert reader.readline() == b"+PONG\r\n"

        sock.sendall(command("SET", "k", "hello\r\nworld"))
        assert reader.readline() == b"+OK\r\n"

        sock.sendall(command("GET", "k"))
        assert reader.read(19) == b"$12\r\nhello\r\nworld\r\n"

        sock.sendall(command("GET", "missing"))
        assert reader.readline() == b"$-1\r\n"

        sock.sendall(command("GET", "too", "many"))
        assert reader.readline() == b"-ERR wrong number of arguments for 'GET' command\r\n"

        sock.sendall(command("BADCMD"))
        assert reader.readline() == b"-ERR unknown command\r\n"


def run_tests():
    compile_server()

    proc = start_server()
    try:
        test_resp_encoded_replies()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v1.1 resp encoder tests passed")
