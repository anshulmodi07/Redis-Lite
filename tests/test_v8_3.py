import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v8_3_bin"
AOF = ROOT / "appendonly.aof"
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


def read_config_array(reader):
    assert read_line(reader).startswith(b"*2")
    key = read_bulk(reader)
    value = read_bulk(reader)
    return key, value


def cleanup():
    for path in (AOF, ROOT / "appendonly.aof.rewrite"):
        if path.exists():
            path.unlink()


def test_appendfsync_config(sock):
    reader = sock.makefile("rb")
    for policy in ("always", "everysec", "no"):
        sock.sendall(command("CONFIG", "SET", "appendfsync", policy))
        assert read_line(reader) == b"+OK\r\n"
        sock.sendall(command("CONFIG", "GET", "appendfsync"))
        _, value = read_config_array(reader)
        assert value == policy.encode()


def test_bgrewrite(sock):
    reader = sock.makefile("rb")
    sock.sendall(command("FLUSHALL"))
    assert read_line(reader) == b"+OK\r\n"
    sock.sendall(command("SET", "k", "v"))
    assert read_line(reader) == b"+OK\r\n"
    sock.sendall(command("INCR", "n"))
    assert read_line(reader) == b":1\r\n"

    sock.sendall(command("BGREWRITEAOF"))
    assert read_line(reader) == b"+Background append only file rewriting started\r\n"
    sock.sendall(command("PING"))
    assert read_line(reader) == b"+PONG\r\n"
    sock.sendall(command("BGREWRITEAOF"))
    reply = read_line(reader)
    assert reply.startswith(b"-ERR"), reply

    deadline = time.time() + 5
    while time.time() < deadline:
        if AOF.exists() and AOF.stat().st_size > 0:
            time.sleep(0.1)
            break
        time.sleep(0.05)
    else:
        raise RuntimeError("AOF rewrite did not finish")


def test_rewrite_restore():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("GET", "k"))
            assert read_bulk(reader) == b"v"
            sock.sendall(command("GET", "n"))
            assert read_bulk(reader) == b"1"
    finally:
        stop_server(proc)


def main():
    cleanup()
    compile_server()
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            sock.settimeout(2)
            test_appendfsync_config(sock)
            test_bgrewrite(sock)
    finally:
        stop_server(proc)

    test_rewrite_restore()
    cleanup()
    print("test_v8_3.py: all tests passed")


if __name__ == "__main__":
    main()
