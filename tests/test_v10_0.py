import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v10_0_bin"
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


def read_bulk(sock_file):
    prefix = read_line(sock_file)
    assert prefix.startswith(b"$"), prefix
    length = int(prefix[1:-2])
    if length == -1:
        return None
    data = sock_file.read(length)
    assert len(data) == length
    assert read_line(sock_file) == b"\r\n"
    return data


def read_resp(sock_file):
    prefix = read_line(sock_file)
    kind = prefix[:1]
    if kind == b"+":
        return ("simple", prefix[1:-2])
    if kind == b"-":
        return ("error", prefix[1:-2])
    if kind == b":":
        return ("int", int(prefix[1:-2]))
    if kind == b"$":
        length = int(prefix[1:-2])
        if length == -1:
            return ("null", None)
        data = sock_file.read(length)
        assert len(data) == length
        assert read_line(sock_file) == b"\r\n"
        return ("bulk", data)
    if kind == b"*":
        count = int(prefix[1:-2])
        if count == -1:
            return ("null-array", None)
        return ("array", [read_resp(sock_file) for _ in range(count)])
    raise AssertionError(prefix)


def test_multi_exec_batch():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("MULTI"))
            assert read_resp(reader) == ("simple", b"OK")

            sock.sendall(command("SET", "tx:a", "1"))
            assert read_resp(reader) == ("simple", b"QUEUED")
            sock.sendall(command("SET", "tx:b", "2"))
            assert read_resp(reader) == ("simple", b"QUEUED")
            sock.sendall(command("GET", "tx:a"))
            assert read_resp(reader) == ("simple", b"QUEUED")

            sock.sendall(command("EXEC"))
            replies = read_resp(reader)
            assert replies[0] == "array"
            items = replies[1]
            assert items[0] == ("simple", b"OK")
            assert items[1] == ("simple", b"OK")
            assert items[2] == ("bulk", b"1")

        with socket.create_connection((HOST, PORT), timeout=2) as verify:
            reader = verify.makefile("rb")
            verify.sendall(command("GET", "tx:b"))
            assert read_resp(reader) == ("bulk", b"2")
    finally:
        stop_server(proc)


def test_discard():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("SET", "tx:discard", "gone"))
            read_resp(reader)

            sock.sendall(command("MULTI"))
            read_resp(reader)
            sock.sendall(command("DEL", "tx:discard"))
            assert read_resp(reader) == ("simple", b"QUEUED")

            sock.sendall(command("DISCARD"))
            assert read_resp(reader) == ("simple", b"OK")

            sock.sendall(command("GET", "tx:discard"))
            assert read_resp(reader) == ("bulk", b"gone")
    finally:
        stop_server(proc)


def test_exec_without_multi():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("EXEC"))
            kind, msg = read_resp(reader)
            assert kind == "error"
            assert b"EXEC without MULTI" in msg
    finally:
        stop_server(proc)


def test_exec_abort_on_queue_error():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("MULTI"))
            read_resp(reader)

            sock.sendall(command("SET", "tx:bad", "x"))
            assert read_resp(reader) == ("simple", b"QUEUED")

            sock.sendall(command("NOTACOMMAND"))
            kind, msg = read_resp(reader)
            assert kind == "error"
            assert b"unknown command" in msg

            sock.sendall(command("EXEC"))
            kind, msg = read_resp(reader)
            assert kind == "error"
            assert b"EXECABORT" in msg

            sock.sendall(command("GET", "tx:bad"))
            assert read_resp(reader) == ("null", None)
    finally:
        stop_server(proc)


def main():
    compile_server()
    test_multi_exec_batch()
    test_discard()
    test_exec_without_multi()
    test_exec_abort_on_queue_error()
    print("test_v10_0.py: all tests passed")


if __name__ == "__main__":
    main()
