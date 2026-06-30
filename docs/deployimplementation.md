# Deploy Implementation Tracker

This document records the actions taken during Phase 0 of the deployment process.

---

## Phase 0 — Repo Hygiene

### 0.1 — Fix unresolved merge conflict ✅

**Status:** Complete

`docs/structure.md` contained unresolved Git conflict markers (`<<<<<<< HEAD`, `=======`,
`>>>>>>> 47531e2b4be68a3b819d30db016f0edfaedf772e`). The file had two identical copies of
the content separated by conflict markers.

**Actions taken:**
- Removed all conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`)
- Kept a single clean copy of the file content (LF line endings, no duplicates)
- Verified the file renders correctly with 37 clean lines

**Files modified:**
- `docs/structure.md`

---

### 0.2 — Reconcile conflicting benchmark numbers ✅

**Status:** Complete

Two benchmark documents existed with different numbers:
- `final_report.md`: SET 13.4k → 61.3k rps (specific to replication backlog O(N) trim fix)
- `analysis.md`: SET ~4.5k baseline, using standardized `redis-benchmark` methodology (later, more rigorous)

**Decision:** `analysis.md` is the canonical, current benchmark document. `final_report.md`
covers a specific optimization (backlog trim fix) and is now archived.

**Actions taken:**
- Created `docs/history/` directory
- Moved `docs/final_report.md` → `docs/history/2026-06-replication-backlog-fix.md`
- Added a "Superseded" notice at the top of the archived file pointing to `analysis.md`
- Deleted the original `docs/final_report.md`

**Files modified:**
- `docs/history/2026-06-replication-backlog-fix.md` (new, archived)
- `docs/final_report.md` (deleted)

---

### 0.3 — Reorganize docs into `docs/` folder ✅

**Status:** Complete — already satisfied

All documentation files (`design_doc.md`, `analysis.md`, `structure.md`, `guide.md`,
`improve.md`, etc.) were already located inside `docs/`. The only `.md` file at repo root
is `README.md`, which is correct.

No file moves were required.

---

### 0.4 — Write canonical top-level README.md ✅

**Status:** Complete

Replaced the old README with a comprehensive, canonical version following the template in
`DEPLOY.md`. Key sections:

- **Features** — full feature list (RESP2, data types, persistence, scripting, clustering)
- **Performance** — honest benchmark numbers from `analysis.md` (not the old inflated numbers)
  - Baseline: SET ~4.5k, GET ~25k (no pipeline)
  - Pipeline P=16: SET ~10.4k, GET ~6.5k
  - Known limitation documented (GET collapse under high concurrency)
- **Architecture** — links to `design_doc.md`
- **Build from source** — actual build commands using `tests/build_sources.py`
- **Project history** — links to `structure.md` and `guide.md`

**Files modified:**
- `README.md` (overwritten with new canonical version)

---

## Phase 0 Summary

| Step | Description | Status |
|------|-------------|--------|
| 0.1 | Fix merge conflict in `structure.md` | ✅ Done |
| 0.2 | Reconcile/archive conflicting benchmark docs | ✅ Done |
| 0.3 | Reorganize docs into `docs/` folder | ✅ Already satisfied |
| 0.4 | Write canonical top-level `README.md` | ✅ Done |

**Phase 0 is complete.** The repo is now clean, conflict-free, with honest benchmark numbers
and a professional README.

---

## Phase 1 — Containerize

### 1.1 — Write the Dockerfile ✅

**Status:** Complete

Created a multi-stage Dockerfile adapted for this project's actual build system
(`tests/build_sources.py` with raw `g++`, not CMake).

**Key decisions:**
- **Build stage:** Ubuntu 24.04 with `g++`, `gcc`, `python3` (needed to run `build_sources.py`).
  No CMake since the project doesn't use it.
- **Build command:** Invokes `build_sources.compile_binary('redis-lite')` directly via Python
  one-liner, producing the binary at `/src/redis-lite`.
- **Runtime stage:** Ubuntu 24.04 with only `libstdc++6`. Non-root user `redislite` (uid 1000).
- **Entrypoint:** `["redis-lite"]` with default `CMD ["--port", "8080"]`.

**Files created:**
- `Dockerfile`

---

### 1.2 — Add .dockerignore ✅

**Status:** Complete

Excludes `.git`, `.github`, `.agents`, cached build artifacts (`tests/.build/`, `tests/__pycache__/`,
test binaries), `docs/`, `*.md`, the 18MB `appendonly.aof`, and test files.

**Files created:**
- `.dockerignore`

---

### 1.3 — Build and smoke-test locally ✅

**Status:** Complete

Built image successfully. Fixed UID 1000 conflict in Ubuntu 24.04 (changed to UID 1001).
Smoke-tested via `docker run` on port 8080.

**Note:** `redis-cli` is not available natively on Windows. Used Docker-based redis-cli:
```bash
docker run --rm -it redis:7.2.7 redis-cli -h host.docker.internal -p 8080 PING
```

---

### 1.4 — docker-compose.yml for benchmarking ✅

**Status:** Complete

Created `docker-compose.yml` with three services:
- `redis-lite` — builds from the Dockerfile, exposes port 8080
- `real-redis` — official Redis 7.2.7 image, in-memory only (`--save "" --appendonly no`)
- `benchmark` — Redis image with `sleep infinity`, used to exec `redis-benchmark` commands

**Usage:**
```bash
docker compose up -d
docker compose exec benchmark redis-benchmark -h redis-lite -p 8080 -t set,get -n 100000 -q
docker compose exec benchmark redis-benchmark -h real-redis -p 6379 -t set,get -n 100000 -q
```

**Files created:**
- `docker-compose.yml`

---

### 1.5 — Push the image ✅

**Status:** Complete

Pushed to Docker Hub as `devam246/redis-lite:v1.0.0`.

```
digest: sha256:876df2318185b0a77dec93e54143e2a5b7b48c4791802c38e2faec350c86a714
```

---

## Phase 1 Summary

| Step | Description | Status |
|------|-------------|--------|
| 1.1 | Write Dockerfile | ✅ Done |
| 1.2 | Add `.dockerignore` | ✅ Done |
| 1.3 | Build and smoke-test locally | ✅ Done |
| 1.4 | Add `docker-compose.yml` | ✅ Done |
| 1.5 | Push image (`devam246/redis-lite:v1.0.0`) | ✅ Done |

**Phase 1 is complete.** Image is live on Docker Hub. Ready for Phase 2 (CI).

---

## Phase 2 — Continuous Integration (GitHub Actions)

### 2.1 — Build + test workflow ✅

**Status:** Complete

Created `.github/workflows/ci.yml` with a single `build-and-test` job that:
1. Installs `g++`, `gcc`, `python3`, `make` (the actual build dependencies)
2. Builds the server binary via `build_sources.compile_binary('redis-lite')` — matching
   the project's real build system (raw `g++`, not CMake)
3. Runs the latest test suite (`test_v12.py`)
4. Runs the benchmark smoke test (`benchmark.py`)

**Triggers:** pushes to `main`, pull requests targeting `main`.

**Files created:**
- `.github/workflows/ci.yml`

---

### 2.2 — Docker build/publish workflow ✅

**Status:** Complete

Created `.github/workflows/docker.yml` that:
1. Builds the Docker image on every push to `main` (verification only)
2. On version tags (`v*`), also pushes to GitHub Container Registry (GHCR) at
   `ghcr.io/anshulmodi07/redis-lite:<tag>`
3. Uses `docker/setup-buildx-action@v3` for modern Buildx support
4. Uses `docker/login-action@v3` with `GITHUB_TOKEN` for GHCR auth (no manual secrets needed)
5. Includes `permissions: packages: write` for GHCR push access

**Files created:**
- `.github/workflows/docker.yml`

---

### 2.3 — Add CI badges to README ✅

**Status:** Complete

Added two badges at the very top of `README.md`, above the title:
- **CI badge** — links to the CI workflow runs
- **Docker badge** — links to the Docker build workflow runs

Badge URLs use the actual repo path: `anshulmodi07/Redis-Lite`.

**Files modified:**
- `README.md`

---

## Phase 2 Summary

| Step | Description | Status |
|------|-------------|--------|
| 2.1 | CI build/test workflow | ✅ Done |
| 2.2 | Docker build/publish workflow | ✅ Done |
| 2.3 | Add CI badges to README | ✅ Done |

**Phase 2 is complete.** CI workflows are set up. Once pushed to GitHub, the badges will
show green/red build status on the README.

---

## Phase 3 — Cloud Deployment (AWS EC2)

### 3.1 — Deploy to AWS EC2 free tier ✅

**Status:** Complete

Deployed Redis Lite on an AWS EC2 `t2.micro` instance (free tier).

**Details:**
- **Elastic IP:** `16.192.114.182`
- **Port:** 8080
- **Image:** `devam246/redis-lite:v1.0.0` (from Docker Hub)
- **Container config:** `--restart unless-stopped` (survives reboots)
- **Security group:** SSH (port 22, restricted), Custom TCP (port 8080, public)

**Verified commands working:**
- `PING` → `PONG`
- `SET hello world` → `OK`
- `GET hello` → `"world"`
- `DEL hello` → `(integer) 1`

**Files created:**
- `scripts/redeploy.sh` — reusable script for future redeployments

---

### 3.4 — Document live demo in README ✅

**Status:** Complete

Added "Quick start" (Docker) and "Live demo" (EC2 IP) sections to `README.md`.

**Files modified:**
- `README.md`

---

## Phase 3 Summary

| Step | Description | Status |
|------|-------------|--------|
| 3.1 | Deploy to AWS EC2 free tier | ✅ Done |
| 3.2 | Fly.io (alternative) | ⏭️ Skipped |
| 3.3 | Browser-based demo | ⏭️ Skipped |
| 3.4 | Document live demo in README | ✅ Done |

**Phase 3 is complete.** Redis Lite is live at `16.192.114.182:8080`.

---

## Phase 4 — Versioning & Release

### 4.1 — Tag the release ✅

**Status:** Complete

Tagged `v1.0.0` with annotated tag and pushed to GitHub. The tag push triggers
the Docker workflow (Phase 2.2) to auto-publish to GHCR.

```bash
git tag -a v1.0.0 -m "Redis Lite v1.0.0 — RESP protocol, persistence, scripting, clustering"
git push origin v1.0.0
```

---

### 4.2 — Write GitHub release notes ✅

**Status:** Complete

Published GitHub release at `https://github.com/anshulmodi07/Redis-Lite/releases/tag/v1.0.0`
with full feature list, honest known limitations, and try-it commands for both Docker and
the live EC2 instance.

---

## Phase 4 Summary

| Step | Description | Status |
|------|-------------|--------|
| 4.1 | Tag v1.0.0 release | ✅ Done |
| 4.2 | Write GitHub release notes | ✅ Done |

**Phase 4 is complete.**

---

## 🎉 All Phases Complete

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | Repo Hygiene | ✅ Done |
| 1 | Containerize | ✅ Done |
| 2 | CI (GitHub Actions) | ✅ Done |
| 3 | Cloud Deployment (AWS EC2) | ✅ Done |
| 4 | Versioning & Release | ✅ Done |

**Redis Lite is fully deployed, CI-tested, containerized, live on AWS, and released as v1.0.0.**
