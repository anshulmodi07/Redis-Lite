#!/bin/bash

set -e

IMAGE="devam246/redis-lite:latest"
CONTAINER="redis-lite"
PORT=8080

echo "==> Pulling latest image..."
docker pull "$IMAGE"

echo "==> Stopping old container..."
docker rm -f "$CONTAINER" 2>/dev/null || true

echo "==> Starting new container..."
docker run -d \
  --name "$CONTAINER" \
  --restart unless-stopped \
  -p "$PORT:$PORT" \
  "$IMAGE"

echo "==> Done! Verifying..."
sleep 2
docker ps --filter "name=$CONTAINER"