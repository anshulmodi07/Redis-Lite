#pragma once

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
};

RedisObject* createStringObject(const std::string& value);
RedisObject* createListObject();
RedisObject* createHashObject();
RedisObject* createSetObject();
RedisObject* createZSetObject();
void destroyObject(RedisObject* obj);

std::string objectTypeName(ObjectType type);
std::string getStringValue(const RedisObject* obj);
