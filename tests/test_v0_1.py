import os
import socket
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER_SRC = ROOT / "server.cpp"
PARSER_SRC = ROOT / "parser.cpp"
RESP_SRC = ROOT / "resp.cpp"
EVENTLOOP_SRC = ROOT / "eventloop.cpp"
SERVER_BIN = ROOT / "tests" / "server_v0_1_bin"
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


def send_command(command):
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        reader = sock.makefile("rb")
        sock.sendall(command)
        return read_line(reader)


def test_basic_commands_and_partial_recv():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        reader = sock.makefile("rb")
        sock.sendall(b"PI")
        time.sleep(0.05)
        sock.sendall(b"NG\n")
        assert read_line(reader) == b"+PONG\r\n"

        sock.sendall(b"SET foo bar\nGET foo\nGET missing\nBADCMD\n")
        assert read_line(reader) == b"+OK\r\n"
        assert reader.read(9) == b"$3\r\nbar\r\n"
        assert read_line(reader) == b"$-1\r\n"
        assert read_line(reader) == b"-ERR unknown command\r\n"


def test_crlf_and_bad_arity():
    assert send_command(b"PING\r\n") == b"+PONG\r\n"
    assert send_command(b"GET too many args\n") == b"-ERR wrong number of arguments for 'GET' command\r\n"


def test_oversized_unterminated_input_disconnects():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        reader = sock.makefile("rb")
        sock.sendall(b"x" * 5000)
        assert read_line(reader) == b"-ERR request too large\r\n"


def run_tests():
    compile_server()

    proc = start_server()
    try:
        test_basic_commands_and_partial_recv()
        test_crlf_and_bad_arity()
        test_oversized_unterminated_input_disconnects()
    finally:
        stop_server(proc)

    restarted = start_server()
    stop_server(restarted)


if __name__ == "__main__":
    run_tests()
    print("v0.1 socket tests passed")
