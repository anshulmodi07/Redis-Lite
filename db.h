#pragma once

#include "object.h"

#include <chrono>
#include <string>
#include <unordered_map>

using Expires = std::unordered_map<std::string, long long>;

struct RedisDb
{
    Db data;
    Expires expires;
};

inline long long nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
