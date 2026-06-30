# Redis Lite — Deployment & Packaging Guide

This document is the step-by-step playbook for turning Redis Lite from a local C++ project
into a polished, containerized, CI-tested, and (optionally) cloud-deployed portfolio piece.

Work through the phases in order. Each phase is independently useful — you can stop after
Phase 2 and already have a strong portfolio repo. Phases 3–4 are the stretch goals.

> **Note on commands below:** the `cmake`/build commands are placeholders based on a typical
> C++17 CMake project. Replace them with your actual build invocation (check whether you have
> a `CMakeLists.txt`, a `Makefile`, or a raw `g++` command in `tests/build_sources.py`) before
> running anything.

---

## Phase 0 — Repo Hygiene (do this first, before anything else)

**Goal:** make the repo trustworthy at a glance. A reviewer decides whether to keep reading
in the first 30 seconds, before they ever run your code.

### 0.1 — Fix the unresolved merge conflict

`structure.md` currently contains unresolved Git conflict markers (`<<<<<<< HEAD`,
`=======`, `>>>>>>>`). This is the single most damaging thing a senior engineer notices in a
portfolio repo — it signals the file was never reviewed before being committed.

```bash
# Open structure.md, remove the conflict markers and duplicate content,
# keep one clean copy of the file, then:
git add structure.md
git commit -m "fix: resolve unresolved merge conflict in structure.md"
git push
```

### 0.2 — Reconcile conflicting benchmark numbers

You currently have two benchmark narratives that disagree:

- `final_report.md`: SET 13.4k → 61.3k rps (replication backlog fix)
- `analysis.md`: SET in the 4k–12k range, GET collapsing under concurrency (later, more
  rigorous `redis-benchmark`-based methodology)

These probably represent different optimization eras and different test methodologies, but
as currently presented side-by-side they look contradictory. Fix:

1. Decide which is the **current, canonical** state of the project (almost certainly
   `analysis.md`, since it's dated later and uses the standardized tool).
2. Move `final_report.md` into `docs/history/` and add a one-line header noting it's
   superseded and what it covered (the backlog-trim fix specifically, not full server perf).
3. Make sure the top-level README states only the current numbers.

```bash
mkdir -p docs/history
git mv final_report.md docs/history/2026-06-replication-backlog-fix.md
```

### 0.3 — Reorganize docs into a `docs/` folder

```bash
mkdir -p docs
git mv design_doc.md docs/design_doc.md
git mv analysis.md docs/analysis.md
git mv structure.md docs/structure.md
git mv guide.md docs/guide.md
git mv improve.md docs/improve.md
git add -A
git commit -m "chore: organize docs into docs/ folder"
```

### 0.4 — Write the canonical top-level README.md

This is the single most important file in the repo. A senior reviewer reads this and decides
whether to clone. Structure it like this:

```markdown
# Redis Lite

A single-threaded, in-memory key-value store implementing the Redis wire protocol (RESP),
built in C++17 with an epoll-based reactor event loop.

## Features
- RESP2 protocol — works with real `redis-cli` and Redis client libraries
- Core data types: String, Hash, List, Set, Sorted Set
- TTL/expiry (active + passive)
- Persistence: RDB snapshotting (fork + COW) and AOF logging
- Pub/Sub, MULTI/EXEC transactions
- Lua scripting (EVAL/EVALSHA) via bundled Lua 5.1
- Basic clustering (hash slots, MOVED, CLUSTER commands)

## Quick start
\`\`\`bash
docker run -p 8080:8080 yourname/redis-lite
redis-cli -p 8080 PING
\`\`\`

## Performance (honest numbers, as of 2026-06-26)
| Scenario | Redis Lite | Real Redis | Gap |
|---|---|---|---|
| SET, no pipeline | ~5.6k req/s | ~76k req/s | ~14× |
| GET, no pipeline | ~30k req/s | ~78k req/s | ~3× |
| SET, pipeline P=16 | ~10.7k req/s | ~625k req/s | ~58× |

Known limitation: GET throughput collapses under high concurrency (C≥50) with pipelining —
see [docs/analysis.md](docs/analysis.md) for full root-cause analysis and next steps.

## Architecture
See [docs/design_doc.md](docs/design_doc.md) for the full architecture diagram and design
rationale (why single-threaded, why skip lists for ZSET, fork+COW for snapshotting, etc.)

## Build from source
[your actual build instructions]

## Project history
This was built incrementally from a thread-per-client toy server to V12. See
[docs/structure.md](docs/structure.md) and [docs/guide.md](docs/guide.md) for the full
version-by-version build log.
```

Commit this before moving to Phase 1 — it's the foundation everything else links back to.

---

## Phase 1 — Containerize

**Goal:** anyone can run your server with one command, with zero C++ toolchain installed.

### 1.1 — Write the Dockerfile

```dockerfile
# Dockerfile
# ---- Build stage ----
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake make ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

# ---- Runtime stage ----
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 && rm -rf /var/lib/apt/lists/*
RUN useradd -m -u 1000 redislite
COPY --from=build /src/build/redis_lite /usr/local/bin/redis_lite
USER redislite
EXPOSE 8080
ENTRYPOINT ["redis_lite"]
CMD ["--port", "8080"]
```

Notes:
- Multi-stage build keeps the final image small (no compiler, no headers in the shipped image).
- Running as a non-root user (`redislite`) is a small but real signal of production awareness.
- Swap the `cmake`/binary path lines for whatever your actual build produces.

### 1.2 — Add a `.dockerignore`

```
.git
.github
build/
tests/.build/
docs/
*.md
```

### 1.3 — Build and smoke-test locally

```bash
docker build -t redis-lite:local .
docker run --rm -p 8080:8080 redis-lite:local

# in another terminal
redis-cli -p 8080 PING
redis-cli -p 8080 SET foo bar
redis-cli -p 8080 GET foo
```

### 1.4 — Add `docker-compose.yml` for side-by-side benchmarking

This lets any reviewer reproduce your `docs/analysis.md` numbers themselves — verifiable
claims are far more convincing than numbers in a table.

```yaml
# docker-compose.yml
services:
  redis-lite:
    build: .
    ports:
      - "8080:8080"
    command: ["--port", "8080"]

  real-redis:
    image: redis:7.2.7
    ports:
      - "6379:6379"
    command: ["redis-server", "--save", "", "--appendonly", "no"]

  benchmark:
    image: redis:7.2.7
    depends_on:
      - redis-lite
      - real-redis
    entrypoint: ["sleep", "infinity"]   # exec into this container to run redis-benchmark
```

```bash
docker compose up -d
docker compose exec benchmark redis-benchmark -h redis-lite -p 8080 -t set,get -n 100000 -q
docker compose exec benchmark redis-benchmark -h real-redis -p 6379 -t set,get -n 100000 -q
```

### 1.5 — Push the image

```bash
docker tag redis-lite:local yourname/redis-lite:v1.0.0
docker tag redis-lite:local yourname/redis-lite:latest
docker login
docker push yourname/redis-lite:v1.0.0
docker push yourname/redis-lite:latest
```

Use Docker Hub (simplest, free for public images) or GitHub Container Registry (`ghcr.io`,
keeps everything under your GitHub identity — slightly more "senior" looking since it's tied
to your repo directly via GitHub Actions, see Phase 2).

---

## Phase 2 — Continuous Integration (GitHub Actions)

**Goal:** a green CI badge on the README — proof of correctness, not a claim of it.

### 2.1 — Build + test workflow

```yaml
# .github/workflows/ci.yml
name: CI

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install build deps
        run: sudo apt-get update && sudo apt-get install -y g++ cmake make

      - name: Build
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build -j"$(nproc)"

      - name: Run test suite
        run: |
          python3 tests/build_sources.py   # if this is a prerequisite step
          python3 -m pytest tests/ -v       # or however test_v11_0.py..test_v12.py are run

      - name: Run benchmark smoke test
        run: |
          ./build/redis_lite --port 8080 &
          sleep 1
          python3 tests/benchmark.py --quick   # adjust flag to whatever short-run option exists
```

### 2.2 — Docker build verification workflow

```yaml
# .github/workflows/docker.yml
name: Docker Build

on:
  push:
    branches: [main]
    tags: ["v*"]

jobs:
  docker:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Log in to GHCR
        if: startsWith(github.ref, 'refs/tags/v')
        uses: docker/login-action@v3
        with:
          registry: ghcr.io
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      - name: Build (and push on tags)
        uses: docker/build-push-action@v5
        with:
          context: .
          push: ${{ startsWith(github.ref, 'refs/tags/v') }}
          tags: |
            ghcr.io/${{ github.repository_owner }}/redis-lite:latest
            ghcr.io/${{ github.repository_owner }}/redis-lite:${{ github.ref_name }}
```

This means: every push builds and tests; every version tag (`v1.0.0`) also builds and
publishes a Docker image automatically — no manual `docker push` needed once this is set up.

### 2.3 — Add the badge to README.md

```markdown
[![CI](https://github.com/yourname/redis-lite/actions/workflows/ci.yml/badge.svg)](https://github.com/yourname/redis-lite/actions/workflows/ci.yml)
[![Docker](https://github.com/yourname/redis-lite/actions/workflows/docker.yml/badge.svg)](https://github.com/yourname/redis-lite/actions/workflows/docker.yml)
```

Put these at the very top of the README, above the description line.

---

## Phase 3 — Cloud Deployment (stretch goal)

**Goal:** a live, publicly reachable instance reviewers can connect to without running
anything locally.

The constraint: Redis Lite speaks raw RESP over TCP, not HTTP, so this isn't a "deploy a web
app" task — it needs a platform that supports raw TCP services. This rules out AWS's
HTTP-centric services (App Runner, Elastic Beanstalk's default ALB setup, API Gateway) which
add complexity for no benefit here. Three options, in order of recommendation given you have
an AWS free tier account:

### 3.1 — Option A: AWS EC2 free tier (recommended for you)

**Why this fits best:** EC2's free tier gives you 750 hours/month of a `t2.micro` or
`t3.micro` instance for 12 months — enough to run continuously, all year, for $0. It's a full
VM, so you get a real public IP and complete control over the TCP port — no load balancer,
no HTTP assumptions, no extra AWS services to wire together. This is the simplest, cheapest,
most "real infrastructure" option available to you right now.

**Step-by-step:**

1. **Launch the instance** (AWS Console → EC2 → Launch Instance):
   - AMI: Ubuntu Server 24.04 LTS (free tier eligible)
   - Instance type: `t2.micro` or `t3.micro` (free tier eligible — check which your account
     shows as eligible, it varies by region)
   - Key pair: create a new one, download the `.pem` file, keep it safe
   - Network settings → create a security group with these inbound rules:
     | Type | Port | Source |
     |---|---|---|
     | SSH | 22 | My IP (not 0.0.0.0/0 — don't open SSH to the world) |
     | Custom TCP | 8080 | 0.0.0.0/0 (so anyone can reach Redis Lite) |
   - Storage: default 8GB gp3 is plenty (free tier covers up to 30GB)
   - Launch.

2. **Allocate an Elastic IP** (so the address doesn't change on reboot):
   - EC2 → Elastic IPs → Allocate → Associate with your instance
   - Free as long as it's attached to a running instance; AWS charges a small fee only if
     it's allocated but *not* attached to anything

3. **Connect and install Docker:**
   ```bash
   chmod 400 your-key.pem
   ssh -i your-key.pem ubuntu@<your-elastic-ip>

   # On the instance:
   sudo apt-get update
   sudo apt-get install -y docker.io
   sudo usermod -aG docker ubuntu
   newgrp docker
   ```

4. **Pull and run your image** (using the one you pushed in Phase 1.5):
   ```bash
   docker pull ghcr.io/yourname/redis-lite:v1.0.0
   docker run -d \
     --name redis-lite \
     --restart unless-stopped \
     -p 8080:8080 \
     ghcr.io/yourname/redis-lite:v1.0.0
   ```
   `--restart unless-stopped` means it survives instance reboots automatically — no systemd
   unit needed, Docker handles it.

5. **Verify from your own machine:**
   ```bash
   redis-cli -h <your-elastic-ip> -p 8080 PING
   ```

6. **(Optional) Point a free subdomain at it** — if you don't want to expose a raw IP in your
   README, services like [DuckDNS](https://www.duckdns.org/) give you a free subdomain
   (`redis-lite.duckdns.org`) pointed at your Elastic IP in about 2 minutes, no AWS Route53
   cost involved.

**Cost check:** `t2.micro`/`t3.micro` + 8GB gp3 storage + one attached Elastic IP, run 24/7,
stays within the free tier for the first 12 months on a new AWS account. Set a [billing
alarm](https://docs.aws.amazon.com/AmazonCloudWatch/latest/monitoring/monitor_estimated_charges_with_cloudwatch.html)
at $1 just to catch anything unexpected — takes 2 minutes and means you'll never be surprised.

**One honest caveat:** unlike Fly.io, EC2 doesn't auto-update when you push a new image tag.
Re-deploying a new version means SSH-ing in and re-running `docker pull` + `docker run`
manually, or writing a small redeploy script. For a portfolio demo that's fine — it's not
meant to be a CD pipeline, just a stable, always-on demo.

### 3.2 — Option B: Fly.io (alternative — zero server management, simplest to *update*)

Fly.io supports arbitrary TCP services (not just HTTP), which is exactly what you need.

```bash
# Install flyctl
curl -L https://fly.io/install.sh | sh

# From your repo root (where the Dockerfile lives)
fly launch --no-deploy
```

This generates a `fly.toml`. Edit it to expose TCP, not HTTP:

```toml
# fly.toml
app = "redis-lite-demo"

[build]

[[services]]
  internal_port = 8080
  protocol = "tcp"

  [[services.ports]]
    port = 8080
    handlers = []   # no TLS termination — raw TCP passthrough

  [[services.tcp_checks]]
    interval = "15s"
    timeout = "2s"
    grace_period = "5s"

[[vm]]
  memory = "256mb"
  cpu_kind = "shared"
  cpus = 1
```

```bash
fly deploy
fly status
```

Once deployed, anyone can run:

```bash
redis-cli -h redis-lite-demo.fly.dev -p 8080 PING
```

Put this exact command in your README — a one-liner a recruiter can paste into a terminal is
worth more than a screenshot.

**Cost note:** Fly.io's free tier covers a small single instance like this comfortably; set
`auto_stop_machines = true` / `min_machines_running = 0` in `fly.toml` if you want it to
scale to zero when idle and avoid any cost.

### 3.3 — Option C: Browser-friendly demo (more impressive, more work)

Since most reviewers won't have `redis-cli` installed, a browser-based terminal makes the
demo accessible to non-technical reviewers too (recruiters, hiring managers skimming on
mobile).

Architecture: `browser (xterm.js) ←WebSocket→ small Node/Python bridge ←TCP→ Redis Lite`

```javascript
// bridge.js — minimal WebSocket-to-TCP proxy
const net = require('net');
const { WebSocketServer } = require('ws');

const wss = new WebSocketServer({ port: 8081 });

wss.on('connection', (ws) => {
  const tcpSocket = net.createConnection(8080, 'localhost');
  tcpSocket.on('data', (data) => ws.send(data.toString()));
  ws.on('message', (msg) => tcpSocket.write(msg.toString() + '\r\n'));
  ws.on('close', () => tcpSocket.end());
  tcpSocket.on('close', () => ws.close());
});
```

Pair this with a static HTML page using [xterm.js](https://xtermjs.org/) for the terminal UI,
deployed wherever — even GitHub Pages for the frontend, with the bridge + server on Fly.io.
This is genuinely more polish than most backend portfolio projects bother with, but treat it
as optional — do 3.1 first and ship that before attempting this.

### 3.4 — Document the live demo in the README

```markdown
## Live demo
\`\`\`bash
redis-cli -h redis-lite-demo.fly.dev -p 8080 PING
\`\`\`
Or try it in-browser: https://yourname.github.io/redis-lite-demo  *(if you built 3.2)*
```

---

## Phase 4 — Versioning & Release

**Goal:** a clean, citable v1.0.0 that documents the project arc.

### 4.1 — Tag the release

```bash
git tag -a v1.0.0 -m "Redis Lite v1.0.0 — RESP protocol, persistence, scripting, clustering"
git push origin v1.0.0
```

This automatically triggers the Docker publish workflow from Phase 2.2 if you set it up with
the `tags: ["v*"]` trigger.

### 4.2 — Write GitHub release notes

Use your existing `docs/structure.md` file-responsibilities list as the basis — it's
effectively your changelog already. Structure the release notes as:

```markdown
## Redis Lite v1.0.0

Built incrementally from a thread-per-client toy TCP server (V0) to a single-threaded
epoll-reactor server implementing the RESP protocol (V12).

### Highlights
- Full RESP2 protocol — compatible with redis-cli and standard client libraries
- Five core data types: String, Hash, List, Set, Sorted Set (skip list + hashtable)
- TTL/expiry: active sweep (100ms cycle) + passive (lazy) expiry
- Persistence: RDB (fork+COW snapshotting) and AOF (command log, configurable fsync)
- Pub/Sub and MULTI/EXEC transactions
- Lua 5.1 scripting via EVAL/EVALSHA/SCRIPT
- Basic clustering: CRC16 hash slots, MOVED redirection, CLUSTER commands

### Known limitations (documented, not hidden)
- GET throughput degrades under high concurrency with deep pipelining (C≥50, P=16) — see
  docs/analysis.md for root cause and the fix roadmap in docs/improve.md
- ~3-60x slower than production Redis depending on scenario — expected for a learning
  implementation without jemalloc, kernel tuning, or years of micro-optimization

### Try it
\`\`\`bash
docker run -p 8080:8080 ghcr.io/yourname/redis-lite:v1.0.0
\`\`\`
```

Stating limitations explicitly in the release notes is a deliberate signal — it's what
distinguishes "I benchmarked this honestly" from "I picked the numbers that looked good."

---

## Checklist Summary

- [ ] Phase 0.1 — Fix merge conflict in `structure.md`
- [ ] Phase 0.2 — Reconcile/archive conflicting benchmark docs
- [ ] Phase 0.3 — Move docs into `docs/` folder
- [ ] Phase 0.4 — Write canonical top-level `README.md`
- [ ] Phase 1.1–1.3 — Dockerfile, build, local smoke test
- [ ] Phase 1.4 — `docker-compose.yml` for reproducible benchmarking
- [ ] Phase 1.5 — Push image to Docker Hub or GHCR
- [ ] Phase 2.1 — CI build/test workflow
- [ ] Phase 2.2 — Docker build/publish workflow
- [ ] Phase 2.3 — Add CI badges to README
- [ ] Phase 3.1 — Deploy to AWS EC2 free tier (recommended)
- [ ] Phase 3.2 — (Alternative) Deploy to Fly.io
- [ ] Phase 3.3 — (Optional) browser-based demo via WebSocket bridge
- [ ] Phase 4.1–4.2 — Tag v1.0.0, write release notes
