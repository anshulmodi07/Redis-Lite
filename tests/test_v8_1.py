import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary, COMPILE_FLAGS, SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v8_1_bin"
DUMP = ROOT / "dump.rdb"
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
    stop_server(proc)
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


def read_line(reader):
    line = reader.readline()
    assert line.endswith(b"\r\n"), line
    return line


def read_bulk(reader):
    prefix = read_line(reader)
    assert prefix.startswith(b"$"), prefix
    length = int(prefix[1:-2])
    if length == -1:
        return None
    data = reader.read(length)
    assert len(data) == length
    assert reader.readline() == b"\r\n"
    return data


def wait_dump(timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if DUMP.exists() and DUMP.stat().st_size > 0:
            time.sleep(0.05)
            return
        time.sleep(0.05)
    raise RuntimeError("dump.rdb not written")


def test_bgsave_nonblocking_and_restore():
    if DUMP.exists():
        DUMP.unlink()

    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("SET", "foo", "bar"))
            assert read_line(reader) == b"+OK\r\n"
            sock.sendall(command("BGSAVE"))
            assert read_line(reader) == b"+Background saving started\r\n"
            sock.sendall(command("PING"))
            assert read_line(reader) == b"+PONG\r\n"
            sock.sendall(command("BGSAVE"))
            reply = read_line(reader)
            assert reply.startswith(b"-ERR"), reply
    finally:
        stop_server(proc)

    wait_dump()

    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("GET", "foo"))
            assert read_bulk(reader) == b"bar"
    finally:
        stop_server(proc)
        if DUMP.exists():
            DUMP.unlink()


def main():
    compile_server()
    test_bgsave_nonblocking_and_restore()
    print("test_v8_1.py: all tests passed")


if __name__ == "__main__":
    main()
