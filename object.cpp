#include "object.h"

#include <stdexcept>

using namespace std;

RedisObject* createStringObject(const string& value)
{
    auto* obj = new RedisObject{
        OBJ_STRING,
        ENC_RAW,
        new string(value)};
    return obj;
}

RedisObject* createListObject()
{
    auto* obj = new RedisObject{
        OBJ_LIST,
        ENC_QUICKLIST,
        new list<string>()};
    return obj;
}

RedisObject* createHashObject()
{
    auto* obj = new RedisObject{
        OBJ_HASH,
        ENC_HASHTABLE,
        new unordered_map<string, string>()};
    return obj;
}

RedisObject* createSetObject()
{
    auto* obj = new RedisObject{
        OBJ_SET,
        ENC_HASHTABLE,
        new unordered_set<string>()};
    return obj;
}

RedisObject* createZSetObject()
{
    auto* obj = new RedisObject{
        OBJ_ZSET,
        ENC_SKIPLIST,
        new unordered_map<string, double>()};
    return obj;
}

void destroyObject(RedisObject* obj)
{
    if (obj == nullptr)
    {
        return;
    }

    switch (obj->type)
    {
    case OBJ_STRING:
        if (obj->encoding == ENC_RAW)
        {
            delete static_cast<string*>(obj->ptr);
        }
        break;
    case OBJ_LIST:
        delete static_cast<list<string>*>(obj->ptr);
        break;
    case OBJ_HASH:
        delete static_cast<unordered_map<string, string>*>(obj->ptr);
        break;
    case OBJ_SET:
        delete static_cast<unordered_set<string>*>(obj->ptr);
        break;
    case OBJ_ZSET:
        delete static_cast<unordered_map<string, double>*>(obj->ptr);
        break;
    }

    delete obj;
}

string objectTypeName(ObjectType type)
{
    switch (type)
    {
    case OBJ_STRING:
        return "string";
    case OBJ_LIST:
        return "list";
    case OBJ_HASH:
        return "hash";
    case OBJ_SET:
        return "set";
    case OBJ_ZSET:
        return "zset";
    }

    return "none";
}

string getStringValue(const RedisObject* obj)
{
    if (obj == nullptr || obj->type != OBJ_STRING || obj->encoding != ENC_RAW)
    {
        throw invalid_argument("value is not a raw string object");
    }

    return *static_cast<const string*>(obj->ptr);
}
