import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary, COMPILE_FLAGS, EXTRA_LIBS, SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v11_1_bin"
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


def test_eval_lock_release():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            script = (
                "if redis.call('get', KEYS[1]) == ARGV[1] then "
                "return redis.call('del', KEYS[1]) else return 0 end"
            )
            sock.sendall(command("SET", "lock:order:42", "my_token"))
            read_line(reader)

            sock.sendall(command("EVAL", script, "1", "lock:order:42", "my_token"))
            assert read_line(reader) == b":1\r\n"

            sock.sendall(command("GET", "lock:order:42"))
            assert read_bulk(reader) is None

            sock.sendall(command("SET", "lock:order:43", "other"))
            read_line(reader)
            sock.sendall(command("EVAL", script, "1", "lock:order:43", "my_token"))
            assert read_line(reader) == b":0\r\n"
    finally:
        stop_server(proc)


def test_script_load():
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("SCRIPT", "LOAD", "return 42"))
            digest = read_bulk(reader)
            assert digest is not None and len(digest) == 40

            sock.sendall(command("EVALSHA", digest.decode(), "0"))
            assert read_line(reader) == b":42\r\n"
    finally:
        stop_server(proc)


def main():
    compile_server()
    test_eval_lock_release()
    test_script_load()
    print("test_v11_1.py: all tests passed")


if __name__ == "__main__":
    main()
