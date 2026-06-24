import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v8_2_bin"
AOF = ROOT / "appendonly.aof"
DUMP = ROOT / "dump.rdb"
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


def read_int(reader):
    line = read_line(reader)
    assert line.startswith(b":"), line
    return int(line[1:-2])


def cleanup():
    for path in (AOF, DUMP):
        if path.exists():
            path.unlink()


def test_aof_append_and_replay():
    cleanup()
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("SET", "session:1", "active"))
            assert read_line(reader) == b"+OK\r\n"
            sock.sendall(command("INCR", "pageviews"))
            assert read_int(reader) == 1
            sock.sendall(command("HSET", "user:1", "email", "test@test.com"))
            assert read_line(reader).startswith(b":")
    finally:
        stop_server(proc)

    assert AOF.exists()
    body = AOF.read_bytes()
    assert b"SET" in body and b"INCR" in body and b"HSET" in body

    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("GET", "session:1"))
            assert read_bulk(reader) == b"active"
            sock.sendall(command("GET", "pageviews"))
            assert read_bulk(reader) == b"1"
            sock.sendall(command("HGET", "user:1", "email"))
            assert read_bulk(reader) == b"test@test.com"
    finally:
        stop_server(proc)
        cleanup()


def main():
    compile_server()
    test_aof_append_and_replay()
    print("test_v8_2.py: all tests passed")


if __name__ == "__main__":
    main()
