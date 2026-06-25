import socket
import time
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERVER_BIN = ROOT / "tests" / "server_v12_bin"
HOST = "127.0.0.1"
PORT = 8080


def start_server():
    proc = subprocess.Popen(
        [str(SERVER_BIN)],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    # Wait for the server to accept connections
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            with socket.create_connection((HOST, PORT), timeout=0.2):
                return proc
        except OSError:
            if proc.poll() is not None:
                raise RuntimeError("server exited early")
            time.sleep(0.05)
    raise RuntimeError("server timed out on startup")


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


def benchmark_no_pipeline(sock, count=1000):
    start = time.time()
    for i in range(count):
        # SET key val
        sock.sendall(command("SET", f"key:{i}", "val"))
        sock.recv(4096)
        # GET key
        sock.sendall(command("GET", f"key:{i}"))
        sock.recv(4096)
    duration = time.time() - start
    ops_sec = (count * 2) / duration
    return ops_sec


def benchmark_pipeline(sock, count=10000, pipeline_size=16):
    start = time.time()
    for i in range(0, count, pipeline_size):
        batch_size = min(pipeline_size, count - i)
        payload = b""
        for j in range(batch_size):
            payload += command("SET", f"pipe:{i+j}", "val")
            payload += command("GET", f"pipe:{i+j}")
        sock.sendall(payload)

        # Read replies
        expected_replies = batch_size * 2
        crlfs_needed = batch_size * 3  # SET (+OK\r\n -> 1 CRLF), GET ($3\r\nval\r\n -> 2 CRLFs)
        crlfs_received = 0
        buf = b""
        while crlfs_received < crlfs_needed:
            data = sock.recv(16384)
            if not data:
                break
            buf += data
            crlfs_received = buf.count(b"\r\n")

    duration = time.time() - start
    ops_sec = (count * 2) / duration
    return ops_sec


def main():
    print("Starting benchmark server...")
    proc = start_server()
    try:
        sock = socket.create_connection((HOST, PORT))
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        print("Running non-pipelining benchmark...")
        ops_no_pipe = benchmark_no_pipeline(sock, count=2000)
        print(f"No pipeline: {ops_no_pipe:.2f} ops/sec")

        print("Running pipelining benchmark (P=16)...")
        ops_pipe = benchmark_pipeline(sock, count=20000, pipeline_size=16)
        print(f"Pipeline (P=16): {ops_pipe:.2f} ops/sec")

        sock.close()
    finally:
        stop_server(proc)


if __name__ == "__main__":
    main()
