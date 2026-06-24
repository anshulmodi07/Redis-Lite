#pragma once

#include "object.h"
#include "skiplist.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

constexpr uint32_t LP_MAX_ENTRIES = 128;
constexpr uint32_t LP_MAX_VALUE_SIZE = 64;
constexpr uint32_t INTSET_MAX_ENTRIES = 512;

std::string objectEncodingName(const RedisObject* obj);

// Hash
long long hashLen(const RedisObject* obj);
bool hashGet(const RedisObject* obj, const std::string& field, std::string& out);
bool hashSet(RedisObject* obj, const std::string& field, const std::string& value, bool& added);
long long hashDel(RedisObject* obj, const std::vector<std::string>& fields);
bool hashExists(const RedisObject* obj, const std::string& field);
std::vector<std::string> hashKeys(const RedisObject* obj);
std::vector<std::string> hashVals(const RedisObject* obj);
std::vector<std::string> hashGetAllFlat(const RedisObject* obj);
bool hashIncrBy(RedisObject* obj, const std::string& field, long long delta, long long& out);

// List
long long listLen(const RedisObject* obj);
void listPushFront(RedisObject* obj, const std::vector<std::string>& values);
void listPushBack(RedisObject* obj, const std::vector<std::string>& values);
std::vector<std::string> listPop(RedisObject* obj, bool from_head, long long count);
std::vector<std::string> listRange(const RedisObject* obj, long long start, long long stop);
bool listIndex(const RedisObject* obj, long long index, std::string& out);
bool listSet(RedisObject* obj, long long index, const std::string& value);
long long listInsert(RedisObject* obj, bool before, const std::string& pivot, const std::string& value);
long long listRem(RedisObject* obj, long long count, const std::string& value);
void listTrim(RedisObject* obj, long long start, long long stop);

// Set
long long setAdd(RedisObject* obj, const std::vector<std::string>& members);
long long setRem(RedisObject* obj, const std::vector<std::string>& members);
long long setCard(const RedisObject* obj);
bool setIsMember(const RedisObject* obj, const std::string& member);
std::vector<std::string> setMembers(const RedisObject* obj);
void setReplaceMembers(RedisObject* obj, const std::vector<std::string>& members);

// ZSet
long long zsetCard(const RedisObject* obj);
bool zsetScore(const RedisObject* obj, const std::string& member, double& out);
long long zsetAdd(
    RedisObject* obj,
    const std::vector<std::pair<double, std::string>>& entries,
    bool nx,
    bool xx,
    bool gt,
    bool lt,
    bool ch,
    long long& added,
    long long& changed);
long long zsetRem(RedisObject* obj, const std::vector<std::string>& members);
long long zsetRank(const RedisObject* obj, const std::string& member, bool reverse, bool& found);
long long zsetCount(const RedisObject* obj, double min, double max);
std::vector<ZSetEntry> zsetRangeByRank(const RedisObject* obj, long long start, long long stop, bool reverse);
std::vector<ZSetEntry> zsetRangeByScore(
    const RedisObject* obj,
    double min,
    double max,
    bool reverse,
    long long offset,
    long long count);
double zsetIncrBy(RedisObject* obj, const std::string& member, double increment);
std::vector<ZSetEntry> zsetPop(RedisObject* obj, long long count, bool max_side);
