import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary, COMPILE_FLAGS, EXTRA_LIBS, SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v11_0_bin"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    compile_binary(SERVER_BIN)


def start_server():
    proc = subprocess.Popen([str(SERVER_BIN)], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            with socket.create_connection((HOST, PORT), timeout=0.2):
                return proc
        except OSError:
            if proc.poll() is not None:
                raise RuntimeError("server exited early")
            time.sleep(0.05)
    proc.terminate()
    raise RuntimeError("server timeout")


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


def read_line(sock_file):
    line = sock_file.readline()
    assert line.endswith(b"\r\n"), line
    return line


def test_pipeline():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            batch = b"".join(command("PING") for _ in range(10))
            sock.sendall(batch)
            for _ in range(10):
                line = read_line(reader)
                assert line in (b"+PONG\r\n", b"$4\r\nPONG\r\n"), line
    finally:
        stop_server(proc)


def main():
    compile_server()
    test_pipeline()
    print("test_v11_0.py: all tests passed")


if __name__ == "__main__":
    main()
