import os
import socket
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    ROOT / "server.cpp",
    ROOT / "parser.cpp",
    ROOT / "resp.cpp",
    ROOT / "sds.cpp",
    ROOT / "object.cpp",
    ROOT / "cmd_string.cpp",
    ROOT / "cmd_hash.cpp",
    ROOT / "cmd_list.cpp",
    ROOT / "cmd_set.cpp",
    ROOT / "cmd_zset.cpp",
    ROOT / "skiplist.cpp",
    ROOT / "eventloop.cpp",
]
SERVER_BIN = ROOT / "tests" / "server_v3_3_bin"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    cxx = os.environ.get("CXX", "g++")
    subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-pthread", "-o", str(SERVER_BIN), *map(str, SOURCES)],
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


def read_bulk_array(reader, count):
  items = []
  for _ in range(count):
      line = read_line(reader)
      assert line.startswith(b"$"), line
      length = int(line[1:-2])
      if length == -1:
          items.append(None)
          continue
      data = reader.read(length + 2)
      assert data.endswith(b"\r\n")
      items.append(data[:-2].decode("utf-8"))
  return items


def test_rpush_lpush_lrange():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("RPUSH", "mylist", "a", "b", "c"))
        assert read_line(reader) == b":3\r\n"

        sock.sendall(command("LPUSH", "mylist", "z"))
        assert read_line(reader) == b":4\r\n"

        sock.sendall(command("LRANGE", "mylist", "0", "-1"))
        assert read_line(reader) == b"*4\r\n"
        assert read_bulk_array(reader, 4) == ["z", "a", "b", "c"]

        sock.sendall(command("LRANGE", "mylist", "1", "2"))
        assert read_line(reader) == b"*2\r\n"
        assert read_bulk_array(reader, 2) == ["a", "b"]


def test_lpop_llen_lindex():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("LPOP", "mylist"))
        assert reader.read(7) == b"$1\r\nz\r\n"

        sock.sendall(command("LLEN", "mylist"))
        assert read_line(reader) == b":3\r\n"

        sock.sendall(command("LINDEX", "mylist", "-1"))
        assert reader.read(7) == b"$1\r\nc\r\n"


def test_lset_lrem_ltrim():
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(1)
        reader = sock.makefile("rb")

        sock.sendall(command("LSET", "mylist", "1", "X"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("RPUSH", "dup", "a", "b", "a", "c", "a"))
        assert read_line(reader) == b":5\r\n"

        sock.sendall(command("LREM", "dup", "2", "a"))
        assert read_line(reader) == b":2\r\n"

        sock.sendall(command("LTRIM", "mylist", "0", "1"))
        assert read_line(reader) == b"+OK\r\n"

        sock.sendall(command("LRANGE", "mylist", "0", "-1"))
        assert read_line(reader) == b"*2\r\n"
        assert read_bulk_array(reader, 2) == ["a", "X"]


def run_tests():
    compile_server()
    proc = start_server()
    try:
        test_rpush_lpush_lrange()
        test_lpop_llen_lindex()
        test_lset_lrem_ltrim()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    run_tests()
    print("v3.3 list command tests passed")
