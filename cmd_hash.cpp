#include "cmd_hash.h"

#include "resp.h"

using namespace std;

namespace
{
using HashMap = unordered_map<string, string>;

string wrongArity(const string& command)
{
    return encodeError("ERR wrong number of arguments for '" + command + "' command");
}

string wrongType()
{
    return encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
}

string notAnInteger()
{
    return encodeError("ERR hash value is not an integer");
}

HashMap* lookupHash(Db& db, const string& key, bool create, bool& type_error)
{
    type_error = false;
    auto it = db.find(key);

    if (it == db.end())
    {
        if (!create)
        {
            return nullptr;
        }

        RedisObject* obj = createHashObject();
        db[key] = obj;
        return static_cast<HashMap*>(obj->ptr);
    }

    if (it->second->type != OBJ_HASH)
    {
        type_error = true;
        return nullptr;
    }

    return static_cast<HashMap*>(it->second->ptr);
}

string commandHSet(const vector<string>& argv, Db& db)
{
    if (argv.size() < 4 || (argv.size() % 2) != 0)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    HashMap* hash = lookupHash(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    long long added = 0;
    for (size_t i = 2; i < argv.size(); i += 2)
    {
        if (hash->find(argv[i]) == hash->end())
        {
            ++added;
        }

        (*hash)[argv[i]] = argv[i + 1];
    }

    return encodeInteger(added);
}

string commandHGet(const vector<string>& argv, const Db& db)
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

    if (it->second->type != OBJ_HASH)
    {
        return wrongType();
    }

    const HashMap* hash = static_cast<const HashMap*>(it->second->ptr);
    auto field = hash->find(argv[2]);
    if (field == hash->end())
    {
        return encodeNullBulk();
    }

    return encodeBulkString(field->second);
}

string commandHMGet(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end())
    {
        string reply = "*" + to_string(argv.size() - 2) + "\r\n";
        for (size_t i = 2; i < argv.size(); ++i)
        {
            reply += encodeNullBulk();
        }
        return reply;
    }

    if (it->second->type != OBJ_HASH)
    {
        return wrongType();
    }

    const HashMap* hash = static_cast<const HashMap*>(it->second->ptr);
    string reply = "*" + to_string(argv.size() - 2) + "\r\n";

    for (size_t i = 2; i < argv.size(); ++i)
    {
        auto field = hash->find(argv[i]);
        if (field == hash->end())
        {
            reply += encodeNullBulk();
        }
        else
        {
            reply += encodeBulkString(field->second);
        }
    }

    return reply;
}

string commandHDel(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    HashMap* hash = lookupHash(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (hash == nullptr)
    {
        return encodeInteger(0);
    }

    long long removed = 0;
    for (size_t i = 2; i < argv.size(); ++i)
    {
        if (hash->erase(argv[i]) > 0)
        {
            ++removed;
        }
    }

    return encodeInteger(removed);
}

string commandHExists(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end() || it->second->type != OBJ_HASH)
    {
        if (it != db.end())
        {
            return wrongType();
        }

        return encodeInteger(0);
    }

    const HashMap* hash = static_cast<const HashMap*>(it->second->ptr);
    return encodeInteger(hash->find(argv[2]) != hash->end() ? 1 : 0);
}

string commandHLen(const vector<string>& argv, const Db& db)
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

    if (it->second->type != OBJ_HASH)
    {
        return wrongType();
    }

    const HashMap* hash = static_cast<const HashMap*>(it->second->ptr);
    return encodeInteger(static_cast<long long>(hash->size()));
}

string commandHKeys(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end())
    {
        return encodeArray({});
    }

    if (it->second->type != OBJ_HASH)
    {
        return wrongType();
    }

    const HashMap* hash = static_cast<const HashMap*>(it->second->ptr);
    vector<string> keys;
    keys.reserve(hash->size());
    for (const auto& entry : *hash)
    {
        keys.push_back(entry.first);
    }

    return encodeArray(keys);
}

string commandHVals(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end())
    {
        return encodeArray({});
    }

    if (it->second->type != OBJ_HASH)
    {
        return wrongType();
    }

    const HashMap* hash = static_cast<const HashMap*>(it->second->ptr);
    vector<string> values;
    values.reserve(hash->size());
    for (const auto& entry : *hash)
    {
        values.push_back(entry.second);
    }

    return encodeArray(values);
}

string commandHGetAll(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end())
    {
        return encodeArray({});
    }

    if (it->second->type != OBJ_HASH)
    {
        return wrongType();
    }

    const HashMap* hash = static_cast<const HashMap*>(it->second->ptr);
    vector<string> flat;
    flat.reserve(hash->size() * 2);
    for (const auto& entry : *hash)
    {
        flat.push_back(entry.first);
        flat.push_back(entry.second);
    }

    return encodeArray(flat);
}

string commandHIncrBy(const vector<string>& argv, Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    HashMap* hash = lookupHash(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    const string& field = argv[2];
    long long delta = stoll(argv[3]);
    auto it = hash->find(field);

    long long current = 0;
    if (it != hash->end())
    {
        if (!tryParseInteger(it->second, current))
        {
            return notAnInteger();
        }
    }

    long long next = current + delta;
    (*hash)[field] = to_string(next);
    return encodeInteger(next);
}
}

string dispatchHashCommand(const vector<string>& argv, Db& db)
{
    const string& cmd = argv[0];

    if (cmd == "HSET" || cmd == "HMSET")
    {
        return commandHSet(argv, db);
    }

    if (cmd == "HGET")
    {
        return commandHGet(argv, db);
    }

    if (cmd == "HMGET")
    {
        return commandHMGet(argv, db);
    }

    if (cmd == "HDEL")
    {
        return commandHDel(argv, db);
    }

    if (cmd == "HEXISTS")
    {
        return commandHExists(argv, db);
    }

    if (cmd == "HLEN")
    {
        return commandHLen(argv, db);
    }

    if (cmd == "HKEYS")
    {
        return commandHKeys(argv, db);
    }

    if (cmd == "HVALS")
    {
        return commandHVals(argv, db);
    }

    if (cmd == "HGETALL")
    {
        return commandHGetAll(argv, db);
    }

    if (cmd == "HINCRBY")
    {
        return commandHIncrBy(argv, db);
    }

    return encodeError("ERR unknown command");
}
