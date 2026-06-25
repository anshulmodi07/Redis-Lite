import os
import socket
import subprocess
import time
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary, COMPILE_FLAGS, CORE_SOURCES, SERVER_SOURCES


ROOT = Path(__file__).resolve().parents[1]
PROBE_SOURCES = CORE_SOURCES
SERVER_BIN = ROOT / "tests" / "server_v5_2_bin"
PROBE_BIN = ROOT / "tests" / "probe_v5_2_bin"
PROBE_SRC = ROOT / "tests" / "probe_v5_2.cpp"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    compile_binary(SERVER_BIN)


def run_probe():
    cxx = os.environ.get("CXX", "g++")
    subprocess.run(
        [
            cxx,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-I",
            str(ROOT),
            str(PROBE_SRC),
            ROOT / "intset.cpp",
            "-o",
            str(PROBE_BIN),
        ],
        check=True,
    )
    subprocess.run([str(PROBE_BIN)], check=True)


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
    length = int(prefix[1:3])
    if length == -1:
        return None
    data = reader.read(length)
    assert len(data) == length, (length, data)
    assert reader.readline() == b"\r\n"
    return data


def test_set_regression():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("SADD", "nums", "1", "3", "2"))
        assert read_line(reader) == b":3\r\n"

        sock.sendall(command("SADD", "nums", "2"))
        assert read_line(reader) == b":0\r\n"

        sock.sendall(command("SISMEMBER", "nums", "3"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("SREM", "nums", "1"))
        assert read_line(reader) == b":1\r\n"

        sock.sendall(command("SCARD", "nums"))
        assert read_line(reader) == b":2\r\n"


def main():
    run_probe()
    compile_server()
    proc = start_server()
    try:
        test_set_regression()
    finally:
        stop_server(proc)
    print("test_v5_2.py: all tests passed")


if __name__ == "__main__":
    main()
