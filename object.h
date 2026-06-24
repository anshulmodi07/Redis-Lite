#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

enum ObjectType
{
    OBJ_STRING,
    OBJ_LIST,
    OBJ_HASH,
    OBJ_SET,
    OBJ_ZSET
};

enum ObjectEncoding
{
    ENC_RAW,
    ENC_INT,
    ENC_LISTPACK,
    ENC_QUICKLIST,
    ENC_HASHTABLE,
    ENC_SKIPLIST,
    ENC_INTSET
};

struct RedisObject
{
    ObjectType type;
    ObjectEncoding encoding;
    void* ptr;
    uint32_t lru = 0;
};

using Db = std::unordered_map<std::string, RedisObject*>;

bool tryParseInteger(const std::string& value, long long& out);

RedisObject* createStringObject(const std::string& value);
RedisObject* createListObject();
RedisObject* createHashObject();
RedisObject* createSetObject();
RedisObject* createZSetObject();
void destroyObject(RedisObject* obj);

std::string objectTypeName(ObjectType type);
std::string getStringValue(const RedisObject* obj);
size_t stringObjectLength(const RedisObject* obj);
bool readStringInteger(const RedisObject* obj, long long& out);
void setStringInteger(RedisObject* obj, long long value);
void setStringValue(RedisObject* obj, const std::string& value);
void appendStringValue(RedisObject* obj, const std::string& suffix);
