#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REDIS_DIR="${HOME}/redis-src"
REDIS_BENCH="${REDIS_DIR}/src/redis-benchmark"
REDIS_SERVER="${REDIS_DIR}/src/redis-server"
LITE_BIN="${ROOT}/tests/server_v12_bin"
LITE_PORT=8080
REAL_PORT=6379
RESULTS="${ROOT}/benchmark_results.txt"

build_redis_tools() {
    if [[ -x "${REDIS_BENCH}" && -x "${REDIS_SERVER}" ]]; then
        return
    fi
    echo "Building redis-benchmark and redis-server..."
    rm -rf "${REDIS_DIR}"
    git clone --depth 1 --branch 7.2.7 https://github.com/redis/redis.git "${REDIS_DIR}"
    make -C "${REDIS_DIR}" MALLOC=libc -j1 redis-benchmark redis-server
}

prepare_lite_data_dir() {
    # Drop benchmark AOF so the server starts instantly.
    rm -f "${ROOT}/appendonly.aof" "${ROOT}/dump.rdb"
}

compile_lite() {
    echo "Compiling Redis Lite..."
    python3 - <<'PY'
from pathlib import Path
import sys
sys.path.insert(0, str(Path("tests")))
from build_sources import compile_binary
compile_binary(Path("tests/server_v12_bin"))
PY
}

wait_for_port() {
    local port=$1
    local deadline=$((SECONDS + 10))
    while (( SECONDS < deadline )); do
        if (echo >/dev/tcp/127.0.0.1/"${port}") 2>/dev/null; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

start_lite() {
    "${LITE_BIN}" >/dev/null 2>&1 &
    LITE_PID=$!
    wait_for_port "${LITE_PORT}" || { echo "Redis Lite failed to start on ${LITE_PORT}"; exit 1; }
}

start_real() {
    "${REDIS_SERVER}" --port "${REAL_PORT}" --save "" --appendonly no >/dev/null 2>&1 &
    REAL_PID=$!
    wait_for_port "${REAL_PORT}" || { echo "Real Redis failed to start on ${REAL_PORT}"; exit 1; }
}

stop_all() {
    [[ -n "${LITE_PID:-}" ]] && kill "${LITE_PID}" 2>/dev/null || true
    [[ -n "${REAL_PID:-}" ]] && kill "${REAL_PID}" 2>/dev/null || true
    wait 2>/dev/null || true
}

run_suite() {
    {
        echo "============================================================"
        echo "Fair Benchmark Suite — $(date -Iseconds)"
        echo "Machine: $(uname -a)"
        echo "Redis Lite binary: ${LITE_BIN}"
        echo "redis-benchmark: ${REDIS_BENCH}"
        echo "Note: appendonly.aof / dump.rdb removed before run for clean startup"
        echo "============================================================"
        echo

        echo "=== 1. Baseline latency (no pipeline, -n 100000) ==="
        echo "--- Real Redis (port ${REAL_PORT}) ---"
        "${REDIS_BENCH}" -p "${REAL_PORT}" -t set,get -n 100000 -q
        echo "--- Redis Lite (port ${LITE_PORT}) ---"
        "${REDIS_BENCH}" -p "${LITE_PORT}" -t set,get -n 100000 -q
        echo

        echo "=== 2. Pipeline scaling curve (Redis Lite, -n 100000) ==="
        for P in 1 4 16 64 256; do
            echo "--- Pipeline P=${P} ---"
            "${REDIS_BENCH}" -p "${LITE_PORT}" -t set,get -n 100000 -P "${P}" -q
        done
        echo

        echo "=== 2b. Pipeline scaling curve (Real Redis, -n 100000) ==="
        for P in 1 4 16 64 256; do
            echo "--- Pipeline P=${P} ---"
            "${REDIS_BENCH}" -p "${REAL_PORT}" -t set,get -n 100000 -P "${P}" -q
        done
        echo

        echo "=== 3. Concurrency scaling (epoll fan-out, -P 16, -n 100000) ==="
        echo "--- Redis Lite ---"
        for C in 1 10 50 100 256; do
            echo "--- Clients C=${C} ---"
            "${REDIS_BENCH}" -p "${LITE_PORT}" -t set,get -n 100000 -c "${C}" -P 16 -q
        done
        echo

        echo "--- Real Redis ---"
        for C in 1 10 50 100 256; do
            echo "--- Clients C=${C} ---"
            "${REDIS_BENCH}" -p "${REAL_PORT}" -t set,get -n 100000 -c "${C}" -P 16 -q
        done
    } | tee "${RESULTS}"
}

trap stop_all EXIT

build_redis_tools
prepare_lite_data_dir
compile_lite
stop_all
# Kill anything already bound to our ports
fuser -k "${LITE_PORT}/tcp" 2>/dev/null || true
fuser -k "${REAL_PORT}/tcp" 2>/dev/null || true
sleep 0.5

start_real
start_lite
run_suite

echo
echo "Results saved to: ${RESULTS}"
