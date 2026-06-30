#!/bin/bash
# redeploy.sh — Pull latest image and restart Redis Lite on EC2
# Usage: ssh into your EC2 instance and run: bash redeploy.sh

set -e

IMAGE="devam246/redis-lite:v1.0.0"
CONTAINER="redis-lite"
PORT=8080

echo "Pulling latest image..."
docker pull "$IMAGE"

echo "Stopping old container (if running)..."
docker rm -f "$CONTAINER" 2>/dev/null || true

echo "Starting new container..."
docker run -d \
  --name "$CONTAINER" \
  --restart unless-stopped \
  -p "$PORT:$PORT" \
  "$IMAGE"

echo "Done! Verifying..."
sleep 2
docker ps --filter "name=$CONTAINER" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
