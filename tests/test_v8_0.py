import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import CORE_SOURCES, SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v8_0_bin"
PROBE_BIN = ROOT / "tests" / "probe_v8_0_bin"
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


def compile_probe():
    cxx = os.environ.get("CXX", "g++")
    probe = ROOT / "tests" / "probe_v8_0.cpp"
    subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-I", str(ROOT), str(probe), *map(str, CORE_SOURCES), "-o", str(PROBE_BIN)],
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


def test_restart_restore():
    if DUMP.exists():
        DUMP.unlink()

    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            for args in (
                ("SET", "foo", "bar"),
                ("SET", "count", "42"),
                ("HSET", "user", "name", "Alice"),
            ):
                sock.sendall(command(*args))
                assert read_line(reader) in {b"+OK\r\n", b":1\r\n", b":2\r\n"}
            sock.sendall(command("SAVE"))
            assert read_line(reader) == b"+OK\r\n"
    finally:
        stop_server(proc)

    assert DUMP.exists() and DUMP.stat().st_size > 0

    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("GET", "foo"))
            assert read_bulk(reader) == b"bar"
            sock.sendall(command("GET", "count"))
            assert read_bulk(reader) == b"42"
            sock.sendall(command("HGET", "user", "name"))
            assert read_bulk(reader) == b"Alice"
    finally:
        stop_server(proc)
        if DUMP.exists():
            DUMP.unlink()


def main():
    compile_probe()
    subprocess.run([str(PROBE_BIN)], check=True)
    compile_server()
    test_restart_restore()
    print("test_v8_0.py: all tests passed")


if __name__ == "__main__":
    main()
