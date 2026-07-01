# Redis Lite Deployment Phase 2
## Production Hardening & Portfolio Roadmap

Current Status: ✅ EC2 Deployment Complete

---

# Current Progress

Completed:

- [x] Docker image published to Docker Hub
- [x] EC2 Ubuntu 24.04 instance created
- [x] Docker installed
- [x] Redis Lite deployed
- [x] Security Groups configured
- [x] Elastic IP configured
- [x] Official redis-cli compatibility verified
- [x] Public endpoint responding with PONG

Deployment endpoint

```
redis-cli -h <Elastic-IP> -p 8080 PING
PONG
```

---

# Phase 1 — Infrastructure

## 1. Billing Protection

Status

- [x] Billing alerts enabled

Pending

- [ ] Wait for Billing metric to appear
- [ ] Create CloudWatch Alarm
- [ ] Threshold = $1
- [ ] Email notification

---

## 2. Elastic IP

Status

- [x] Attached
- [x] Static endpoint available

---

## 3. Docker Deployment

Current

```
docker run ...
```

Upgrade To

```
Docker Compose
```

Benefits

- easier deployments
- version controlled deployment
- cleaner configuration
- production standard

---

# Phase 2 — Persistent Storage

Current Situation

appendonly.aof currently exists only during local execution.

Inside Docker no persistence exists yet.

Goal

```
Host Directory
      │
      ▼
Docker Volume
      │
      ▼
/data
      │
      ▼
appendonly.aof
```

Tasks

- [ ] Make AOF path configurable
- [ ] Add AOF_PATH environment variable
- [ ] Create /data directory
- [ ] Mount persistent Docker volume
- [ ] Verify persistence after restart

Example

```yaml
volumes:
  - ./data:/data

environment:
  AOF_PATH: /data/appendonly.aof
```

---

# Phase 3 — Application Improvements

Current

```
appendonly.aof
```

Target

```cpp
const char* env = std::getenv("AOF_PATH");
std::string path =
    env ? env : "appendonly.aof";
```

Benefits

- configurable
- Docker friendly
- production ready

Additional improvements

- [ ] configurable port
- [ ] configurable data directory
- [ ] graceful shutdown
- [ ] SIGTERM handling
- [ ] better logging
- [ ] startup banner

---

# Phase 4 — Docker Improvements

Current

Single stage image

Upgrade

Multi-stage build

Goals

- smaller image
- faster deployment
- less attack surface

Dockerfile improvements

- [ ] WORKDIR /app
- [ ] Create /data
- [ ] HEALTHCHECK
- [ ] Non-root user
- [ ] Multi-stage compilation

---

# Phase 5 — Docker Compose

Create

```
deploy/

compose.yaml
redeploy.sh
```

compose.yaml

```yaml
services:

  redis-lite:

    image: devam246/redis-lite:v1.1.0

    restart: unless-stopped

    ports:

      - "8080:8080"

    volumes:

      - ./data:/data

    environment:

      AOF_PATH: /data/appendonly.aof
```

Benefits

- one command deployment
- automatic restart
- reproducible deployments

---

# Phase 6 — Redeployment Script

Current

Manual commands

Target

```
./redeploy.sh
```

Script

```
docker compose pull

docker compose up -d

docker image prune -f
```

One command deployment.

---

# Phase 7 — CI/CD

Current

Manual

Future

```
Git Push

↓

GitHub Actions

↓

Docker Build

↓

Docker Hub

↓

SSH EC2

↓

docker compose pull

↓

docker compose up -d
```

Tasks

- [ ] GitHub Actions
- [ ] Docker login secrets
- [ ] EC2 SSH secrets
- [ ] Automated deployment

---

# Phase 8 — Monitoring

Health Check

```
redis-cli ping
```

Future

Docker Healthcheck

```dockerfile
HEALTHCHECK CMD redis-cli -p 8080 ping
```

Monitoring

- [ ] Docker health
- [ ] CloudWatch metrics
- [ ] CPU
- [ ] Memory
- [ ] Disk usage

---

# Phase 9 — Domain Name

Current

```
16.xxx.xxx.xxx
```

Target

```
redislite-devam.duckdns.org
```

Tasks

- [ ] Create DuckDNS domain
- [ ] Point to Elastic IP
- [ ] Update README

---

# Phase 10 — Security

Current

Port 8080 open publicly

Future

- [ ] Restrict SSH to My IP
- [ ] Optional authentication
- [ ] Firewall review
- [ ] Non-root Docker user

---

# Phase 11 — Benchmarking

Using

```
redis-benchmark
```

Benchmarks

```
SET

GET

PING

Concurrent clients

Pipeline
```

Results

| Command | Requests/sec | Notes |
|----------|--------------|------|
| SET | | |
| GET | | |
| PING | | |

---

# Phase 12 — Documentation

README additions

- [ ] Architecture diagram
- [ ] Deployment diagram
- [ ] Docker instructions
- [ ] AWS guide
- [ ] Docker Hub
- [ ] Live demo
- [ ] Benchmarks
- [ ] Screenshots

---

# Phase 13 — Architecture Diagram

```
            Client

               │

               ▼

        redis-cli

               │

               ▼

          Elastic IP

               │

               ▼

      AWS Security Group

               │

               ▼

        Docker Compose

               │

               ▼

        Redis Lite

               │

               ▼

        appendonly.aof

               │

               ▼

      Persistent Volume
```

---

# Phase 14 — Future Features

Protocol

- [ ] RESP3

Data Structures

- [ ] Lists
- [ ] Sets
- [ ] Sorted Sets
- [ ] Hashes
- [ ] Streams

Persistence

- [ ] Snapshotting
- [ ] AOF Rewrite

Networking

- [ ] Thread Pool
- [ ] Async Networking
- [ ] Replication

Performance

- [ ] Memory Pool
- [ ] Object Encoding
- [ ] LRU Cache

---

# Release Plan

## v1.0

- Docker
- EC2
- Public Deployment

Status

✅ Complete

---

## v1.1

- Docker Compose
- Persistent Storage
- Configurable AOF
- Redeploy Script

Status

🚧 Planned

---

## v1.2

- GitHub Actions
- Auto Deployment
- DuckDNS
- Healthcheck

Status

📅 Planned

---

## v2.0

- Replication
- Transactions
- Pub/Sub
- Additional Data Structures

Status

🔮 Future

---

# End Goal

A production-grade Redis-compatible server with:

- Public AWS deployment
- Persistent storage
- CI/CD pipeline
- Docker Compose
- Health monitoring
- Automated redeployment
- Professional documentation
- Benchmark results
- Resume-quality architecture