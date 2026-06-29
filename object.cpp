#include "object.h"

#include "eviction.h"
#include "intset.h"
#include "listpack.h"
#include "sds.h"
#include "skiplist.h"

#include <stdexcept>

using namespace std;

namespace
{
std::vector<RedisObject*> object_pools[5];

RedisObject* allocObjectShell(ObjectType type)
{
    auto& pool = object_pools[static_cast<size_t>(type)];
    if (!pool.empty())
    {
        RedisObject* obj = pool.back();
        pool.pop_back();
        return obj;
    }

    return new RedisObject{};
}

void releaseObjectShell(RedisObject* obj)
{
    if (obj == nullptr)
    {
        return;
    }

    object_pools[static_cast<size_t>(obj->type)].push_back(obj);
}

RedisObject* makeObject(ObjectType type, ObjectEncoding encoding, void* ptr)
{
    RedisObject* obj = allocObjectShell(type);
    obj->type = type;
    obj->encoding = encoding;
    obj->ptr = ptr;
    obj->lru = 0;
    touchObject(obj);
    return obj;
}
}

// bool tryParseInteger(const string& value, long long& out)
// {
//     if (value.empty())
//     {
//         return false;
//     }

//     try
//     {
//         size_t consumed = 0;
//         out = stoll(value, &consumed);
//         return consumed == value.size();
//     }
//     catch (const exception&)
//     {
//         return false;
//     }
// }

#include <climits>

bool tryParseInteger(const std::string& value, long long& out)
{
    if (value.empty())
        return false;

    size_t i = 0;
    bool negative = false;

    if (value[0] == '-' || value[0] == '+')
    {
        negative = (value[0] == '-');
        i++;
    }

    if (i == value.size())
        return false;

    long long result = 0;

    

    for (; i < value.size(); ++i)
    {
        char c = value[i];

        if (c < '0' || c > '9')
            return false;

        int digit = c - '0';

        // Overflow check
        if (result > (LLONG_MAX - digit) / 10)
            return false;

        result = result * 10 + digit;
    }

    out = negative ? -result : result;
    return true;
}

RedisObject* createStringObject(const string& value)
{
    long long integer = 0;
    if (tryParseInteger(value, integer))
    {
        return makeObject(OBJ_STRING, ENC_INT, new long long(integer));
    }

    return makeObject(OBJ_STRING, ENC_RAW, sdsnewlen(value.data(), value.size()));
}

RedisObject* createListObject()
{
    return makeObject(OBJ_LIST, ENC_LISTPACK, lpNew());
}

RedisObject* createHashObject()
{
    return makeObject(OBJ_HASH, ENC_LISTPACK, lpNew());
}

RedisObject* createSetObject()
{
    return makeObject(OBJ_SET, ENC_INTSET, intsetNew());
}

RedisObject* createZSetObject()
{
    return makeObject(OBJ_ZSET, ENC_LISTPACK, lpNew());
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

    obj->ptr = nullptr;
    releaseObjectShell(obj);
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

    if (obj->encoding == ENC_INT)
    {
        *static_cast<long long*>(obj->ptr) = value;
        return;
    }

    sdsfree(static_cast<sds>(obj->ptr));
    obj->encoding = ENC_INT;
    obj->ptr = new long long(value);
}

void setStringValue(RedisObject* obj, const string& value)
{
    if (obj == nullptr || obj->type != OBJ_STRING)
        throw invalid_argument("value is not a string object");

    long long integer = 0;

    // ---------- New value is an integer ----------
    if (tryParseInteger(value, integer))
    {
        // INT -> INT (reuse allocation)
        if (obj->encoding == ENC_INT)
        {
            *static_cast<long long*>(obj->ptr) = integer;
            return;
        }

        // RAW -> INT
        sdsfree(static_cast<sds>(obj->ptr));

        obj->encoding = ENC_INT;
        obj->ptr = new long long(integer);
        return;
    }

    // ---------- New value is a string ----------
    if (obj->encoding == ENC_RAW)
    {
        sds s = static_cast<sds>(obj->ptr);

        sdsclear(s);                      // O(1)
        s = sdscatlen(s, value.data(), value.size());

        if (s == nullptr)
            throw bad_alloc();

        obj->ptr = s;
        return;
    }

    // ---------- INT -> RAW ----------
    delete static_cast<long long*>(obj->ptr);

    obj->encoding = ENC_RAW;
    obj->ptr = sdsnewlen(value.data(), value.size());

    if (obj->ptr == nullptr)
        throw bad_alloc();
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
