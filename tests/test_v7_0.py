import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v7_0_bin"
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


def read_int(reader):
    line = read_line(reader)
    assert line.startswith(b":"), line
    return int(line[1:-2])


def test_config(sock):
    reader = sock.makefile("rb")
    sock.sendall(command("CONFIG", "SET", "maxmemory-policy", "allkeys-lru"))
    assert read_line(reader) == b"+OK\r\n"
    sock.sendall(command("CONFIG", "GET", "maxmemory-policy"))
    assert read_line(reader).startswith(b"*2")


def test_eviction(sock):
    reader = sock.makefile("rb")
    sock.sendall(command("FLUSHALL"))
    assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("CONFIG", "SET", "maxmemory-policy", "allkeys-lru"))
    assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("CONFIG", "SET", "maxmemory", "4096"))
    assert read_line(reader) == b"+OK\r\n"

    inserted = 0
    for i in range(200):
        sock.sendall(command("SET", f"key{i}", "x" * 64))
        reply = read_line(reader)
        if reply.startswith(b"-OOM"):
            break
        assert reply == b"+OK\r\n", reply
        inserted += 1

    sock.sendall(command("DBSIZE"))
    size = read_int(reader)
    assert size < 200
    assert inserted > 0


def test_noeviction_oom(sock):
    reader = sock.makefile("rb")
    sock.sendall(command("FLUSHALL"))
    assert read_line(reader) == b"+OK\r\n"
    sock.sendall(command("CONFIG", "SET", "maxmemory-policy", "noeviction"))
    assert read_line(reader) == b"+OK\r\n"
    sock.sendall(command("CONFIG", "SET", "maxmemory", "256"))
    assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("SET", "big", "x" * 128))
    first = read_line(reader)
    if first == b"+OK\r\n":
        sock.sendall(command("SET", "big2", "y" * 128))
        second = read_line(reader)
        assert second.startswith(b"-OOM"), second


def main():
    compile_server()
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            sock.settimeout(2)
            test_config(sock)
            test_eviction(sock)
            test_noeviction_oom(sock)
    finally:
        stop_server(proc)
    print("test_v7_0.py: all tests passed")


if __name__ == "__main__":
    main()
