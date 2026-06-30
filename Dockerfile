# Dockerfile
# ---- Build stage ----
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ gcc python3 make ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Build using the project's own build script (raw g++, not CMake)
RUN python3 -c "\
import sys; sys.path.insert(0, 'tests'); \
from build_sources import compile_binary; \
compile_binary('redis-lite')"

# ---- Runtime stage ----
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -u 1001 redislite

COPY --from=build /src/redis-lite /usr/local/bin/redis-lite

USER redislite
EXPOSE 8080
ENTRYPOINT ["redis-lite"]
CMD ["--port", "8080"]
