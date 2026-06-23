#include "cmd_list.h"

#include "resp.h"

#include <list>

using namespace std;

namespace
{
using RedisList = list<string>;

string wrongArity(const string& command)
{
    return encodeError("ERR wrong number of arguments for '" + command + "' command");
}

string wrongType()
{
    return encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
}

long long normalizeIndex(long long index, long long size)
{
    if (index < 0)
    {
        index = size + index;
    }

    return index;
}

bool indexInRange(long long index, long long size)
{
    return index >= 0 && index < size;
}

RedisList* lookupList(Db& db, const string& key, bool create, bool& type_error)
{
    type_error = false;
    auto it = db.find(key);

    if (it == db.end())
    {
        if (!create)
        {
            return nullptr;
        }

        RedisObject* obj = createListObject();
        db[key] = obj;
        return static_cast<RedisList*>(obj->ptr);
    }

    if (it->second->type != OBJ_LIST)
    {
        type_error = true;
        return nullptr;
    }

    return static_cast<RedisList*>(it->second->ptr);
}

list<string>::iterator listAt(RedisList& list, long long index)
{
    auto it = list.begin();
    for (long long i = 0; i < index; ++i)
    {
        ++it;
    }

    return it;
}

string commandLPush(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisList* list = lookupList(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    for (size_t i = 2; i < argv.size(); ++i)
    {
        list->push_front(argv[i]);
    }

    return encodeInteger(static_cast<long long>(list->size()));
}

string commandRPush(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisList* list = lookupList(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    for (size_t i = 2; i < argv.size(); ++i)
    {
        list->push_back(argv[i]);
    }

    return encodeInteger(static_cast<long long>(list->size()));
}

string popElements(RedisList* list, bool from_head, long long count)
{
    vector<string> popped;
    popped.reserve(static_cast<size_t>(count));

    for (long long i = 0; i < count && !list->empty(); ++i)
    {
        if (from_head)
        {
            popped.push_back(list->front());
            list->pop_front();
        }
        else
        {
            popped.push_back(list->back());
            list->pop_back();
        }
    }

    if (count == 1)
    {
        if (popped.empty())
        {
            return encodeNullBulk();
        }

        return encodeBulkString(popped.front());
    }

    return encodeArray(popped);
}

string commandLPop(const vector<string>& argv, Db& db)
{
    if (argv.size() < 2 || argv.size() > 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisList* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    long long count = 1;
    if (argv.size() == 3)
    {
        count = stoll(argv[2]);
        if (count < 0)
        {
            return encodeError("ERR value is out of range, or is not an integer");
        }
    }

    if (list == nullptr || list->empty())
    {
        if (argv.size() == 3)
        {
            return encodeArray({});
        }

        return encodeNullBulk();
    }

    return popElements(list, true, count);
}

string commandRPop(const vector<string>& argv, Db& db)
{
    if (argv.size() < 2 || argv.size() > 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisList* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    long long count = 1;
    if (argv.size() == 3)
    {
        count = stoll(argv[2]);
        if (count < 0)
        {
            return encodeError("ERR value is out of range, or is not an integer");
        }
    }

    if (list == nullptr || list->empty())
    {
        if (argv.size() == 3)
        {
            return encodeArray({});
        }

        return encodeNullBulk();
    }

    return popElements(list, false, count);
}

string commandLLen(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end())
    {
        return encodeInteger(0);
    }

    if (it->second->type != OBJ_LIST)
    {
        return wrongType();
    }

    const RedisList* list = static_cast<const RedisList*>(it->second->ptr);
    return encodeInteger(static_cast<long long>(list->size()));
}

string commandLRange(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end())
    {
        return encodeArray({});
    }

    if (it->second->type != OBJ_LIST)
    {
        return wrongType();
    }

    const RedisList* list = static_cast<const RedisList*>(it->second->ptr);
    long long size = static_cast<long long>(list->size());
    if (size == 0)
    {
        return encodeArray({});
    }

    long long start = normalizeIndex(stoll(argv[2]), size);
    long long stop = normalizeIndex(stoll(argv[3]), size);

    if (start < 0)
    {
        start = 0;
    }

    if (stop >= size)
    {
        stop = size - 1;
    }

    if (start > stop)
    {
        return encodeArray({});
    }

    vector<string> slice;
    long long index = 0;
    for (const string& value : *list)
    {
        if (index >= start && index <= stop)
        {
            slice.push_back(value);
        }

        ++index;
    }

    return encodeArray(slice);
}

string commandLIndex(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end())
    {
        return encodeNullBulk();
    }

    if (it->second->type != OBJ_LIST)
    {
        return wrongType();
    }

    const RedisList* list = static_cast<const RedisList*>(it->second->ptr);
    long long size = static_cast<long long>(list->size());
    long long index = normalizeIndex(stoll(argv[2]), size);

    if (!indexInRange(index, size))
    {
        return encodeNullBulk();
    }

    long long pos = 0;
    for (const string& value : *list)
    {
        if (pos == index)
        {
            return encodeBulkString(value);
        }

        ++pos;
    }

    return encodeNullBulk();
}

string commandLSet(const vector<string>& argv, Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisList* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (list == nullptr)
    {
        return encodeError("ERR no such key");
    }

    long long size = static_cast<long long>(list->size());
    long long index = normalizeIndex(stoll(argv[2]), size);

    if (!indexInRange(index, size))
    {
        return encodeError("ERR index out of range");
    }

    *listAt(*list, index) = argv[3];
    return encodeOK();
}

string commandLInsert(const vector<string>& argv, Db& db)
{
    if (argv.size() != 5)
    {
        return wrongArity(argv[0]);
    }

    const string& where = argv[2];
    if (where != "BEFORE" && where != "AFTER")
    {
        return encodeError("ERR syntax error");
    }

    bool type_error = false;
    RedisList* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (list == nullptr)
    {
        return encodeInteger(0);
    }

    for (auto it = list->begin(); it != list->end(); ++it)
    {
        if (*it != argv[3])
        {
            continue;
        }

        if (where == "BEFORE")
        {
            list->insert(it, argv[4]);
        }
        else
        {
            list->insert(next(it), argv[4]);
        }

        return encodeInteger(static_cast<long long>(list->size()));
    }

    return encodeInteger(0);
}

string commandLRem(const vector<string>& argv, Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisList* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (list == nullptr)
    {
        return encodeInteger(0);
    }

    long long count = stoll(argv[2]);
    const string& value = argv[3];
    long long removed = 0;

    if (count == 0)
    {
        for (auto it = list->begin(); it != list->end();)
        {
            if (*it == value)
            {
                it = list->erase(it);
                ++removed;
            }
            else
            {
                ++it;
            }
        }

        return encodeInteger(removed);
    }

    if (count > 0)
    {
        for (auto it = list->begin(); it != list->end() && count > 0;)
        {
            if (*it == value)
            {
                it = list->erase(it);
                ++removed;
                --count;
            }
            else
            {
                ++it;
            }
        }
    }
    else
    {
        long long abs_count = -count;
        for (long long i = static_cast<long long>(list->size()) - 1; i >= 0 && abs_count > 0; --i)
        {
            auto it = list->begin();
            for (long long pos = 0; pos < i; ++pos)
            {
                ++it;
            }

            if (*it == value)
            {
                list->erase(it);
                ++removed;
                --abs_count;
            }
        }
    }

    return encodeInteger(removed);
}

string commandLTrim(const vector<string>& argv, Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisList* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (list == nullptr || list->empty())
    {
        return encodeOK();
    }

    long long size = static_cast<long long>(list->size());
    long long start = normalizeIndex(stoll(argv[2]), size);
    long long stop = normalizeIndex(stoll(argv[3]), size);

    if (start < 0)
    {
        start = 0;
    }

    if (stop >= size)
    {
        stop = size - 1;
    }

    if (start > stop)
    {
        list->clear();
        return encodeOK();
    }

    RedisList trimmed;
    long long index = 0;
    for (const string& value : *list)
    {
        if (index >= start && index <= stop)
        {
            trimmed.push_back(value);
        }

        ++index;
    }

    *list = std::move(trimmed);
    return encodeOK();
}
}

string dispatchListCommand(const vector<string>& argv, Db& db)
{
    const string& cmd = argv[0];

    if (cmd == "LPUSH")
    {
        return commandLPush(argv, db);
    }

    if (cmd == "RPUSH")
    {
        return commandRPush(argv, db);
    }

    if (cmd == "LPOP")
    {
        return commandLPop(argv, db);
    }

    if (cmd == "RPOP")
    {
        return commandRPop(argv, db);
    }

    if (cmd == "LLEN")
    {
        return commandLLen(argv, db);
    }

    if (cmd == "LRANGE")
    {
        return commandLRange(argv, db);
    }

    if (cmd == "LINDEX")
    {
        return commandLIndex(argv, db);
    }

    if (cmd == "LSET")
    {
        return commandLSet(argv, db);
    }

    if (cmd == "LINSERT")
    {
        return commandLInsert(argv, db);
    }

    if (cmd == "LREM")
    {
        return commandLRem(argv, db);
    }

    if (cmd == "LTRIM")
    {
        return commandLTrim(argv, db);
    }

    return encodeError("ERR unknown command");
}
