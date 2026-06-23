#include "object.h"

#include "skiplist.h"

#include <stdexcept>

using namespace std;

bool tryParseInteger(const string& value, long long& out)
{
    if (value.empty())
    {
        return false;
    }

    try
    {
        size_t consumed = 0;
        out = stoll(value, &consumed);
        return consumed == value.size();
    }
    catch (const exception&)
    {
        return false;
    }
}

RedisObject* createStringObject(const string& value)
{
    long long integer = 0;
    if (tryParseInteger(value, integer))
    {
        return new RedisObject{
            OBJ_STRING,
            ENC_INT,
            new long long(integer)};
    }

    return new RedisObject{
        OBJ_STRING,
        ENC_RAW,
        new string(value)};
}

RedisObject* createListObject()
{
    return new RedisObject{
        OBJ_LIST,
        ENC_QUICKLIST,
        new list<string>()};
}

RedisObject* createHashObject()
{
    return new RedisObject{
        OBJ_HASH,
        ENC_HASHTABLE,
        new unordered_map<string, string>()};
}

RedisObject* createSetObject()
{
    return new RedisObject{
        OBJ_SET,
        ENC_HASHTABLE,
        new unordered_set<string>()};
}

RedisObject* createZSetObject()
{
    return new RedisObject{
        OBJ_ZSET,
        ENC_SKIPLIST,
        new ZSet()};
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
        else if (obj->encoding == ENC_INT)
        {
            delete static_cast<long long*>(obj->ptr);
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
        delete static_cast<ZSet*>(obj->ptr);
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
    if (obj == nullptr || obj->type != OBJ_STRING)
    {
        throw invalid_argument("value is not a string object");
    }

    if (obj->encoding == ENC_INT)
    {
        return to_string(*static_cast<const long long*>(obj->ptr));
    }

    if (obj->encoding == ENC_RAW)
    {
        return *static_cast<const string*>(obj->ptr);
    }

    throw invalid_argument("unsupported string encoding");
}

size_t stringObjectLength(const RedisObject* obj)
{
    return getStringValue(obj).size();
}

bool readStringInteger(const RedisObject* obj, long long& out)
{
    if (obj == nullptr || obj->type != OBJ_STRING)
    {
        return false;
    }

    if (obj->encoding == ENC_INT)
    {
        out = *static_cast<const long long*>(obj->ptr);
        return true;
    }

    if (obj->encoding == ENC_RAW)
    {
        return tryParseInteger(getStringValue(obj), out);
    }

    return false;
}

void setStringInteger(RedisObject* obj, long long value)
{
    if (obj == nullptr || obj->type != OBJ_STRING)
    {
        throw invalid_argument("value is not a string object");
    }

    if (obj->encoding == ENC_RAW)
    {
        delete static_cast<string*>(obj->ptr);
    }
    else if (obj->encoding == ENC_INT)
    {
        delete static_cast<long long*>(obj->ptr);
    }

    obj->encoding = ENC_INT;
    obj->ptr = new long long(value);
}

void setStringValue(RedisObject* obj, const string& value)
{
    if (obj == nullptr || obj->type != OBJ_STRING)
    {
        throw invalid_argument("value is not a string object");
    }

    if (obj->encoding == ENC_RAW)
    {
        delete static_cast<string*>(obj->ptr);
    }
    else if (obj->encoding == ENC_INT)
    {
        delete static_cast<long long*>(obj->ptr);
    }

    long long integer = 0;
    if (tryParseInteger(value, integer))
    {
        obj->encoding = ENC_INT;
        obj->ptr = new long long(integer);
        return;
    }

    obj->encoding = ENC_RAW;
    obj->ptr = new string(value);
}
