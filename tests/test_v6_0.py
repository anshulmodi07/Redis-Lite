import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v6_0_bin"
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


def command(*parts):
    frame = f"*{len(parts)}\r\n".encode("utf-8")
    for part in parts:
        data = part.encode("utf-8")
        frame += f"${len(data)}\r\n".encode("utf-8") + data + b"\r\n"
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
    assert len(data) == length, (length, data)
    assert reader.readline() == b"\r\n"
    return data


def read_int(reader):
    line = read_line(reader)
    assert line.startswith(b":"), line
    return int(line[1:-2])


def read_array(reader):
    prefix = read_line(reader)
    assert prefix.startswith(b"*"), prefix
    count = int(prefix[1:-2])
    return [read_bulk(reader) for _ in range(count)]


def test_utilities(sock):
    reader = sock.makefile("rb")

    sock.sendall(command("PING"))
    assert read_line(reader) == b"+PONG\r\n"

    sock.sendall(command("ECHO", "hello"))
    assert read_bulk(reader) == b"hello"

    sock.sendall(command("SET", "a", "1"))
    assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("DBSIZE"))
    assert read_int(reader) == 1

    sock.sendall(command("EXISTS", "a", "missing"))
    assert read_int(reader) == 1

    sock.sendall(command("TYPE", "a"))
    assert read_line(reader) == b"+string\r\n"

    sock.sendall(command("OBJECT", "ENCODING", "a"))
    assert read_bulk(reader) in {b"raw", b"int"}


def test_select(sock):
    reader = sock.makefile("rb")

    sock.sendall(command("SET", "only0", "x"))
    assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("SELECT", "1"))
    assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("DBSIZE"))
    assert read_int(reader) == 0

    sock.sendall(command("SET", "only1", "y"))
    assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("SELECT", "0"))
    assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("GET", "only0"))
    assert read_bulk(reader) == b"x"

    sock.sendall(command("GET", "only1"))
    assert read_bulk(reader) is None


def read_scan(reader):
    assert read_line(reader).startswith(b"*2")
    read_bulk(reader)
    prefix = read_line(reader)
    assert prefix.startswith(b"*"), prefix
    count = int(prefix[1:-2])
    return [read_bulk(reader) for _ in range(count)]


def test_keys_scan_rename(sock):
    reader = sock.makefile("rb")

    sock.sendall(command("FLUSHDB"))
    assert read_line(reader) == b"+OK\r\n"

    for key in ("user:1", "user:2", "post:1"):
        sock.sendall(command("SET", key, "v"))
        assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("KEYS", "user:*"))
    keys = read_array(reader)
    assert sorted(keys) == [b"user:1", b"user:2"]

    sock.sendall(command("SCAN", "0", "MATCH", "user:*", "COUNT", "1"))
    batch = read_scan(reader)
    assert len(batch) == 1

    sock.sendall(command("RENAME", "post:1", "post:moved"))
    assert read_line(reader) == b"+OK\r\n"

    sock.sendall(command("GET", "post:moved"))
    assert read_bulk(reader) == b"v"

    sock.sendall(command("RENAMENX", "user:1", "user:2"))
    assert read_int(reader) == 0


def test_regression(sock):
    reader = sock.makefile("rb")

    sock.sendall(command("INCR", "counter"))
    assert read_int(reader) == 1

    sock.sendall(command("HSET", "h", "f", "1"))
    assert read_int(reader) == 1

    sock.sendall(command("ZADD", "z", "1", "m"))
    assert read_int(reader) == 1


def main():
    compile_server()
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            sock.settimeout(2)
            test_utilities(sock)
            test_select(sock)
            test_keys_scan_rename(sock)
            test_regression(sock)
    finally:
        stop_server(proc)
    print("test_v6_0.py: all tests passed")


if __name__ == "__main__":
    main()
