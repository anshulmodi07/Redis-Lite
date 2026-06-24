#pragma once

#include "db.h"
#include "object.h"

#include <cstddef>
#include <string>
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
};

extern ServerConfig g_server_config;

void touchObject(RedisObject* obj);
size_t estimateServerMemory(const std::vector<RedisDb>& databases);
std::string ensureMemoryForWrite(const std::vector<RedisDb>& databases);
std::string evictionPolicyName(EvictionPolicy policy);
bool parseEvictionPolicy(const std::string& value, EvictionPolicy& out);
bool parseMemoryBytes(const std::string& value, size_t& out);
