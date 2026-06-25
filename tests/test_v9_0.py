import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary, COMPILE_FLAGS, SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v9_0_bin"
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


def read_array(sock_file):
    prefix = read_line(sock_file)
    assert prefix.startswith(b"*"), prefix
    count = int(prefix[1:-2])
    return [read_bulk(sock_file) for _ in range(count)]


def test_publish_subscribe():
    proc = start_server()
    got = threading.Event()
    error = []

    def subscriber():
        try:
            sock = socket.create_connection((HOST, PORT), timeout=2)
            sock.settimeout(2)
            reader = sock.makefile("rb")
            sock.sendall(command("SUBSCRIBE", "news"))
            parts = read_array(reader)
            assert parts[0] == b"subscribe" and parts[1] == b"news"
            parts = read_array(reader)
            assert parts == [b"message", b"news", b"breaking"]
            got.set()
        except Exception as exc:
            error.append(exc)

    t = threading.Thread(target=subscriber)
    t.start()
    time.sleep(0.15)

    try:
        with socket.create_connection((HOST, PORT), timeout=2) as pub:
            reader = pub.makefile("rb")
            pub.sendall(command("PUBLISH", "news", "breaking"))
            line = read_line(reader)
            assert line.startswith(b":1"), line
    finally:
        t.join(timeout=3)
        stop_server(proc)

    assert not error, error
    assert got.is_set()


def test_psubscribe_and_pubsub():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as pub:
            reader = pub.makefile("rb")
            pub.sendall(command("PUBLISH", "newsroom", "hi"))
            assert read_line(reader).startswith(b":0")

        with socket.create_connection((HOST, PORT), timeout=2) as sub:
            sub.settimeout(2)
            reader = sub.makefile("rb")
            sub.sendall(command("PSUBSCRIBE", "news*"))
            read_array(reader)

            with socket.create_connection((HOST, PORT), timeout=2) as pub:
                pub.sendall(command("PUBLISH", "newsroom", "hi"))
                pub.recv(64)

            parts = read_array(reader)
            assert parts[:2] == [b"pmessage", b"news*"]
            assert parts[2] == b"newsroom" and parts[3] == b"hi"

        sub = socket.create_connection((HOST, PORT), timeout=2)
        try:
            sub_reader = sub.makefile("rb")
            sub.sendall(command("SUBSCRIBE", "ch1"))
            read_array(sub_reader)

            with socket.create_connection((HOST, PORT), timeout=2) as admin:
                reader = admin.makefile("rb")
                admin.sendall(command("PUBSUB", "NUMSUB", "ch1"))
                parts = read_array(reader)
                assert parts == [b"ch1", b"1"]
        finally:
            sub.close()
    finally:
        stop_server(proc)


def main():
    compile_server()
    test_publish_subscribe()
    test_psubscribe_and_pubsub()
    print("test_v9_0.py: all tests passed")


if __name__ == "__main__":
    main()
