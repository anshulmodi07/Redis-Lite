#include "eviction.h"

#include "encoding.h"
#include "sds.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <limits>

using namespace std;

ServerConfig g_server_config;

namespace
{
uint32_t nowLruClock()
{
    using namespace chrono;
    auto secs = duration_cast<std::chrono::seconds>(system_clock::now().time_since_epoch()).count();
    return static_cast<uint32_t>(secs & 0xFFFFFF);
}

size_t estimateStringSize(const RedisObject* obj)
{
    if (obj->encoding == ENC_INT)
    {
        return sizeof(long long);
    }

    return sdslen(static_cast<sds>(obj->ptr)) + 32;
}

size_t estimateObjectSize(const RedisObject* obj)
{
    size_t size = sizeof(RedisObject) + 64;

    switch (obj->type)
    {
    case OBJ_STRING:
        return size + estimateStringSize(obj);
    case OBJ_HASH:
        return size + static_cast<size_t>(hashLen(obj)) * 48;
    case OBJ_LIST:
        return size + static_cast<size_t>(listLen(obj)) * 32;
    case OBJ_SET:
        return size + static_cast<size_t>(setCard(obj)) * 32;
    case OBJ_ZSET:
        return size + static_cast<size_t>(zsetCard(obj)) * 40;
    default:
        return size;
    }
}

size_t estimateDbMemory(const RedisDb& db)
{
    size_t total = 0;
    for (const auto& item : db.data)
    {
        total += item.first.size() + estimateObjectSize(item.second) + 64;
    }

    return total;
}

bool isVolatileKey(const RedisDb& db, const string& key)
{
    return db.expires.find(key) != db.expires.end();
}

bool poolAllowsKey(EvictionPolicy policy, const RedisDb& db, const string& key)
{
    switch (policy)
    {
    case EvictionPolicy::VolatileLru:
    case EvictionPolicy::VolatileRandom:
    case EvictionPolicy::VolatileTtl:
    case EvictionPolicy::VolatileLfu:
        return isVolatileKey(db, key);
    default:
        return true;
    }
}

void deleteKey(RedisDb& db, const string& key)
{
    auto it = db.data.find(key);
    if (it == db.data.end())
    {
        return;
    }

    destroyObject(it->second);
    db.data.erase(it);
    db.expires.erase(key);
}

bool evictOneKey(vector<RedisDb>& databases)
{
    EvictionPolicy policy = g_server_config.maxmemory_policy;
    if (policy == EvictionPolicy::NoEviction)
    {
        return false;
    }

    vector<size_t> populated;
    for (size_t i = 0; i < databases.size(); ++i)
    {
        if (!databases[i].data.empty())
        {
            populated.push_back(i);
        }
    }

    if (populated.empty())
    {
        return false;
    }

    RedisDb& db = databases[populated[static_cast<size_t>(rand()) % populated.size()]];
    vector<string> pool;
    pool.reserve(db.data.size());
    for (const auto& item : db.data)
    {
        if (poolAllowsKey(policy, db, item.first))
        {
            pool.push_back(item.first);
        }
    }

    if (pool.empty())
    {
        return false;
    }

    if (policy == EvictionPolicy::AllKeysRandom || policy == EvictionPolicy::VolatileRandom)
    {
        deleteKey(db, pool[static_cast<size_t>(rand()) % pool.size()]);
        return true;
    }

    size_t samples = min(g_server_config.maxmemory_samples, pool.size());
    string victim = pool[0];
    uint32_t best_lru = UINT32_MAX;
    long long best_ttl = INT64_MAX;

    for (size_t i = 0; i < samples; ++i)
    {
        const string& key = pool[static_cast<size_t>(rand()) % pool.size()];
        auto it = db.data.find(key);
        if (it == db.data.end())
        {
            continue;
        }

        if (policy == EvictionPolicy::VolatileTtl)
        {
            long long ttl = ttlMilliseconds(db, key);
            if (ttl < 0)
            {
                ttl = INT64_MAX;
            }

            if (ttl < best_ttl)
            {
                best_ttl = ttl;
                victim = key;
            }

            continue;
        }

        if (it->second->lru < best_lru)
        {
            best_lru = it->second->lru;
            victim = key;
        }
    }

    deleteKey(db, victim);
    return true;
}
}

void touchObject(RedisObject* obj)
{
    if (obj != nullptr)
    {
        obj->lru = nowLruClock();
    }
}

size_t estimateServerMemory(const vector<RedisDb>& databases)
{
    size_t total = 0;
    for (const RedisDb& db : databases)
    {
        total += estimateDbMemory(db);
    }

    return total;
}

string ensureMemoryForWrite(const vector<RedisDb>& databases)
{
    if (g_server_config.maxmemory == 0)
    {
        return {};
    }

    vector<RedisDb>& mutable_dbs = const_cast<vector<RedisDb>&>(databases);
    while (estimateServerMemory(databases) > g_server_config.maxmemory)
    {
        if (!evictOneKey(mutable_dbs))
        {
            return "-OOM command not allowed when used memory > 'maxmemory'.\r\n";
        }
    }

    return {};
}

string evictionPolicyName(EvictionPolicy policy)
{
    switch (policy)
    {
    case EvictionPolicy::NoEviction:
        return "noeviction";
    case EvictionPolicy::AllKeysLru:
        return "allkeys-lru";
    case EvictionPolicy::VolatileLru:
        return "volatile-lru";
    case EvictionPolicy::AllKeysRandom:
        return "allkeys-random";
    case EvictionPolicy::VolatileRandom:
        return "volatile-random";
    case EvictionPolicy::VolatileTtl:
        return "volatile-ttl";
    case EvictionPolicy::AllKeysLfu:
        return "allkeys-lfu";
    case EvictionPolicy::VolatileLfu:
        return "volatile-lfu";
    default:
        return "noeviction";
    }
}

bool parseEvictionPolicy(const string& value, EvictionPolicy& out)
{
    string normalized;
    normalized.reserve(value.size());
    for (char ch : value)
    {
        normalized.push_back(static_cast<char>(tolower(static_cast<unsigned char>(ch))));
    }

    if (normalized == "noeviction")
    {
        out = EvictionPolicy::NoEviction;
    }
    else if (normalized == "allkeys-lru")
    {
        out = EvictionPolicy::AllKeysLru;
    }
    else if (normalized == "volatile-lru")
    {
        out = EvictionPolicy::VolatileLru;
    }
    else if (normalized == "allkeys-random")
    {
        out = EvictionPolicy::AllKeysRandom;
    }
    else if (normalized == "volatile-random")
    {
        out = EvictionPolicy::VolatileRandom;
    }
    else if (normalized == "volatile-ttl")
    {
        out = EvictionPolicy::VolatileTtl;
    }
    else if (normalized == "allkeys-lfu")
    {
        out = EvictionPolicy::AllKeysLfu;
    }
    else if (normalized == "volatile-lfu")
    {
        out = EvictionPolicy::VolatileLfu;
    }
    else
    {
        return false;
    }

    return true;
}

bool parseMemoryBytes(const string& value, size_t& out)
{
    if (value.empty())
    {
        return false;
    }

    size_t pos = 0;
    while (pos < value.size() && isdigit(static_cast<unsigned char>(value[pos])))
    {
        ++pos;
    }

    if (pos == 0)
    {
        return false;
    }

    size_t number = 0;
    try
    {
        number = static_cast<size_t>(stoull(value.substr(0, pos)));
    }
    catch (...)
    {
        return false;
    }

    string unit;
    for (; pos < value.size(); ++pos)
    {
        char ch = static_cast<char>(tolower(static_cast<unsigned char>(value[pos])));
        if (!isspace(static_cast<unsigned char>(ch)))
        {
            unit.push_back(ch);
        }
    }

    size_t multiplier = 1;
    if (unit.empty() || unit == "b")
    {
        multiplier = 1;
    }
    else if (unit == "k" || unit == "kb")
    {
        multiplier = 1024;
    }
    else if (unit == "m" || unit == "mb")
    {
        multiplier = 1024 * 1024;
    }
    else if (unit == "g" || unit == "gb")
    {
        multiplier = 1024ULL * 1024 * 1024;
    }
    else
    {
        return false;
    }

    out = number * multiplier;
    return true;
}
