import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v10_1_bin"
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


def test_watch_exec_success():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("SET", "watch:ok", "old"))
            read_resp(reader)

            sock.sendall(command("WATCH", "watch:ok"))
            assert read_resp(reader) == ("simple", b"OK")
            sock.sendall(command("MULTI"))
            read_resp(reader)
            sock.sendall(command("SET", "watch:ok", "new"))
            assert read_resp(reader) == ("simple", b"QUEUED")
            sock.sendall(command("EXEC"))
            replies = read_resp(reader)
            assert replies[0] == "array"
            assert replies[1][0] == ("simple", b"OK")

        with socket.create_connection((HOST, PORT), timeout=2) as verify:
            reader = verify.makefile("rb")
            verify.sendall(command("GET", "watch:ok"))
            assert read_resp(reader) == ("bulk", b"new")
    finally:
        stop_server(proc)


def test_watch_exec_aborted_by_other_client():
    proc = start_server()
    ready = threading.Event()
    interference_done = threading.Event()
    errors = []

    def watcher():
        try:
            sock = socket.create_connection((HOST, PORT), timeout=2)
            sock.settimeout(2)
            reader = sock.makefile("rb")
            sock.sendall(command("SET", "watch:race", "start"))
            read_resp(reader)

            sock.sendall(command("WATCH", "watch:race"))
            read_resp(reader)
            sock.sendall(command("MULTI"))
            read_resp(reader)
            sock.sendall(command("SET", "watch:race", "winner"))
            assert read_resp(reader) == ("simple", b"QUEUED")

            ready.set()
            assert interference_done.wait(timeout=3)

            sock.sendall(command("EXEC"))
            assert read_resp(reader) == ("null-array", None)
            sock.close()
        except Exception as exc:
            errors.append(exc)

    t = threading.Thread(target=watcher)
    t.start()
    assert ready.wait(timeout=3)

    try:
        with socket.create_connection((HOST, PORT), timeout=2) as interferer:
            reader = interferer.makefile("rb")
            interferer.sendall(command("SET", "watch:race", "interference"))
            read_resp(reader)
        interference_done.set()
        t.join(timeout=3)
        assert not errors, errors

        with socket.create_connection((HOST, PORT), timeout=2) as verify:
            reader = verify.makefile("rb")
            verify.sendall(command("GET", "watch:race"))
            assert read_resp(reader) == ("bulk", b"interference")
    finally:
        stop_server(proc)


def test_own_write_before_multi_does_not_abort():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("WATCH", "watch:self"))
            read_resp(reader)
            sock.sendall(command("SET", "watch:self", "mine"))
            read_resp(reader)
            sock.sendall(command("MULTI"))
            read_resp(reader)
            sock.sendall(command("GET", "watch:self"))
            assert read_resp(reader) == ("simple", b"QUEUED")
            sock.sendall(command("EXEC"))
            replies = read_resp(reader)
            assert replies[0] == "array"
            assert replies[1][0] == ("bulk", b"mine")
    finally:
        stop_server(proc)


def main():
    compile_server()
    test_watch_exec_success()
    test_watch_exec_aborted_by_other_client()
    test_own_write_before_multi_does_not_abort()
    print("test_v10_1.py: all tests passed")


if __name__ == "__main__":
    main()
