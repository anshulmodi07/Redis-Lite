#pragma once

#include "db.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

enum class EvictionPolicy
{
    NoEviction,
    AllKeysLru,
    VolatileLru,
    AllKeysRandom,
    VolatileRandom,
    VolatileTtl,
    AllKeysLfu,
    VolatileLfu
};

struct ServerConfig
{
    size_t maxmemory = 0;
    EvictionPolicy maxmemory_policy = EvictionPolicy::NoEviction;
    size_t maxmemory_samples = 5;
    int port = 8080;
    bool readonly_replica = false;
    std::string replicaof_host;
    int replicaof_port = 0;
    bool cluster_enabled = false;
    uint16_t cluster_bus_port = 0;
    std::string cluster_id;
    std::string cluster_announce_ip = "127.0.0.1";
    std::vector<std::pair<uint16_t, uint16_t>> cluster_slots;
};

extern ServerConfig g_server_config;

bool parseServerArgs(int argc, char** argv);

void touchObject(RedisObject* obj);
size_t estimateServerMemory(const std::vector<RedisDb>& databases);
std::string ensureMemoryForWrite(const std::vector<RedisDb>& databases);
std::string evictionPolicyName(EvictionPolicy policy);
bool parseEvictionPolicy(const std::string& value, EvictionPolicy& out);
bool parseMemoryBytes(const std::string& value, size_t& out);
