import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build_sources import compile_binary

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v12_bin"
HOST = "127.0.0.1"
PORT = 8080


def compile_server():
    compile_binary(SERVER_BIN)


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
        data = str(part).encode("utf-8")
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


def read_array(reader):
    prefix = read_line(reader)
    assert prefix.startswith(b"*"), prefix
    count = int(prefix[1:-2])
    return [read_bulk(reader) for _ in range(count)]


def parse_info_response(data_bytes):
    info = {}
    lines = data_bytes.decode("utf-8").split("\r\n")
    for line in lines:
        if not line or line.startswith("#"):
            continue
        parts = line.split(":", 1)
        if len(parts) == 2:
            info[parts[0]] = parts[1]
    return info


def test_info_command(sock):
    reader = sock.makefile("rb")

    # Send general INFO command (should return all sections)
    sock.sendall(command("INFO"))
    resp = read_bulk(reader)
    assert resp is not None
    info = parse_info_response(resp)

    assert "redis_version" in info, info
    assert "redis_mode" in info, info
    assert "uptime_in_seconds" in info, info
    assert "connected_clients" in info, info
    assert "used_memory" in info, info
    assert "mem_fragmentation_ratio" in info, info
    assert "total_commands_processed" in info, info
    assert "ops_per_sec" in info, info

    # Send specific INFO sections
    sock.sendall(command("INFO", "server"))
    resp_server = read_bulk(reader)
    info_server = parse_info_response(resp_server)
    assert "redis_version" in info_server
    assert "connected_clients" not in info_server

    sock.sendall(command("INFO", "clients"))
    resp_clients = read_bulk(reader)
    info_clients = parse_info_response(resp_clients)
    assert "connected_clients" in info_clients
    assert "used_memory" not in info_clients

    sock.sendall(command("INFO", "memory"))
    resp_memory = read_bulk(reader)
    info_memory = parse_info_response(resp_memory)
    assert "used_memory" in info_memory
    assert "total_commands_processed" not in info_memory

    sock.sendall(command("INFO", "stats"))
    resp_stats = read_bulk(reader)
    info_stats = parse_info_response(resp_stats)
    assert "total_commands_processed" in info_stats
    assert "redis_version" not in info_stats


def test_config_get_all(sock):
    reader = sock.makefile("rb")

    # CONFIG GET *
    sock.sendall(command("CONFIG", "GET", "*"))
    arr = read_array(reader)
    assert len(arr) >= 8, arr
    
    config_dict = {}
    for i in range(0, len(arr), 2):
        config_dict[arr[i].decode("utf-8")] = arr[i+1].decode("utf-8")

    assert "maxmemory" in config_dict
    assert "maxmemory-policy" in config_dict
    assert "maxmemory-samples" in config_dict
    assert "appendfsync" in config_dict


def test_stats_tracking(sock):
    reader = sock.makefile("rb")

    # Send a few commands to increment processed count
    for _ in range(5):
        sock.sendall(command("PING"))
        assert read_line(reader) == b"+PONG\r\n"

    sock.sendall(command("INFO", "stats"))
    resp_stats = read_bulk(reader)
    info_stats = parse_info_response(resp_stats)
    
    total_cmds = int(info_stats.get("total_commands_processed", 0))
    # We sent 5 PINGs, 1 INFO, plus whatever other tests did
    assert total_cmds >= 6


def main():
    compile_server()
    proc = start_server()
    try:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            sock.settimeout(2)
            test_info_command(sock)
            test_config_get_all(sock)
            test_stats_tracking(sock)
    finally:
        stop_server(proc)
    print("test_v12.py: all tests passed")


if __name__ == "__main__":
    main()
