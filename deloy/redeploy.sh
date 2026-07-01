#!/bin/bash
# redeploy.sh — Pull latest image and restart Redis Lite on EC2 via Docker Compose
# Usage: cd ~/deploy && bash redeploy.sh

set -e

COMPOSE_FILE="$(dirname "$0")/compose.yaml"

echo "==> Pulling latest image..."
docker compose -f "$COMPOSE_FILE" pull

echo "==> Restarting service..."
docker compose -f "$COMPOSE_FILE" up -d

echo "==> Pruning old images..."
docker image prune -f

echo "==> Done! Verifying..."
sleep 2
docker compose -f "$COMPOSE_FILE" ps
