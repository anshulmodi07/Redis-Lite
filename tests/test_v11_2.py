import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary, COMPILE_FLAGS, EXTRA_LIBS, SERVER_SOURCES

ROOT = Path(__file__).resolve().parents[1]
MASTER_BIN = ROOT / "tests" / "server_v11_2_master_bin"
REPLICA_BIN = ROOT / "tests" / "server_v11_2_replica_bin"
HOST = "127.0.0.1"
MASTER_PORT = 8080
REPLICA_PORT = 8081


def compile_server(out_path):
    compile_binary(out_path)


def start_server(bin_path, extra_args):
    proc = subprocess.Popen([str(bin_path), *extra_args], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    port = MASTER_PORT if "--port" not in extra_args else int(extra_args[extra_args.index("--port") + 1])
    deadline = time.time() + 8
    while time.time() < deadline:
        try:
            with socket.create_connection((HOST, port), timeout=0.2):
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


def test_replication():
    master = start_server(MASTER_BIN, [])
    replica = start_server(REPLICA_BIN, ["--port", str(REPLICA_PORT), "--replicaof", HOST, str(MASTER_PORT)])
    try:
        time.sleep(1.5)
        with socket.create_connection((HOST, MASTER_PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("SET", "repl:key", "synced"))
            assert read_line(reader) == b"+OK\r\n"

        time.sleep(1.0)
        with socket.create_connection((HOST, REPLICA_PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("GET", "repl:key"))
            assert read_bulk(reader) == b"synced"

        with socket.create_connection((HOST, REPLICA_PORT), timeout=2) as sock:
            reader = sock.makefile("rb")
            sock.sendall(command("SET", "repl:readonly", "nope"))
            line = read_line(reader)
            assert line.startswith(b"-"), line
            assert b"READONLY" in line
    finally:
        stop_server(replica)
        stop_server(master)


def main():
    compile_server(MASTER_BIN)
    compile_server(REPLICA_BIN)
    test_replication()
    print("test_v11_2.py: all tests passed")


if __name__ == "__main__":
    main()
