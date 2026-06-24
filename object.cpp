#include "object.h"

#include "intset.h"
#include "listpack.h"
#include "sds.h"
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
        sdsnewlen(value.data(), value.size())};
}

RedisObject* createListObject()
{
    return new RedisObject{
        OBJ_LIST,
        ENC_LISTPACK,
        lpNew()};
}

RedisObject* createHashObject()
{
    return new RedisObject{
        OBJ_HASH,
        ENC_LISTPACK,
        lpNew()};
}

RedisObject* createSetObject()
{
    return new RedisObject{
        OBJ_SET,
        ENC_INTSET,
        intsetNew()};
}

RedisObject* createZSetObject()
{
    return new RedisObject{
        OBJ_ZSET,
        ENC_LISTPACK,
        lpNew()};
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
            sdsfree(static_cast<sds>(obj->ptr));
        }
        else if (obj->encoding == ENC_INT)
        {
            delete static_cast<long long*>(obj->ptr);
        }
        break;
    case OBJ_LIST:
        if (obj->encoding == ENC_LISTPACK)
        {
            lpFree(static_cast<listpack>(obj->ptr));
        }
        else
        {
            delete static_cast<list<string>*>(obj->ptr);
        }
        break;
    case OBJ_HASH:
        if (obj->encoding == ENC_LISTPACK)
        {
            lpFree(static_cast<listpack>(obj->ptr));
        }
        else
        {
            delete static_cast<unordered_map<string, string>*>(obj->ptr);
        }
        break;
    case OBJ_SET:
        if (obj->encoding == ENC_INTSET)
        {
            intsetFree(static_cast<intset>(obj->ptr));
        }
        else if (obj->encoding == ENC_LISTPACK)
        {
            lpFree(static_cast<listpack>(obj->ptr));
        }
        else
        {
            delete static_cast<unordered_set<string>*>(obj->ptr);
        }
        break;
    case OBJ_ZSET:
        if (obj->encoding == ENC_LISTPACK)
        {
            lpFree(static_cast<listpack>(obj->ptr));
        }
        else
        {
            delete static_cast<ZSet*>(obj->ptr);
        }
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
        const sds raw = static_cast<sds>(obj->ptr);
        return string(raw, sdslen(raw));
    }

    throw invalid_argument("unsupported string encoding");
}

size_t stringObjectLength(const RedisObject* obj)
{
    if (obj == nullptr || obj->type != OBJ_STRING)
    {
        throw invalid_argument("value is not a string object");
    }

    if (obj->encoding == ENC_INT)
    {
        return to_string(*static_cast<const long long*>(obj->ptr)).size();
    }

    if (obj->encoding == ENC_RAW)
    {
        return sdslen(static_cast<sds>(obj->ptr));
    }

    throw invalid_argument("unsupported string encoding");
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
        sdsfree(static_cast<sds>(obj->ptr));
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
        sdsfree(static_cast<sds>(obj->ptr));
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
    obj->ptr = sdsnewlen(value.data(), value.size());
}

void appendStringValue(RedisObject* obj, const string& suffix)
{
    if (obj == nullptr || obj->type != OBJ_STRING)
    {
        throw invalid_argument("value is not a string object");
    }

    if (obj->encoding == ENC_INT)
    {
        const string current = to_string(*static_cast<long long*>(obj->ptr));
        delete static_cast<long long*>(obj->ptr);
        obj->encoding = ENC_RAW;
        obj->ptr = sdsnewlen(current.data(), current.size());
    }

    obj->ptr = sdscatlen(static_cast<sds>(obj->ptr), suffix.data(), suffix.size());

    const string merged = getStringValue(obj);
    long long integer = 0;
    if (tryParseInteger(merged, integer))
    {
        sdsfree(static_cast<sds>(obj->ptr));
        obj->encoding = ENC_INT;
        obj->ptr = new long long(integer);
    }
}
