#include "cmd_hash.h"

#include "encoding.h"
#include "resp.h"

using namespace std;

namespace
{
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

RedisObject* lookupHash(Db& db, const string& key, bool create, bool& type_error)
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
        return obj;
    }

    if (it->second->type != OBJ_HASH)
    {
        type_error = true;
        return nullptr;
    }

    return it->second;
}

string commandHSet(const vector<string>& argv, Db& db)
{
    if (argv.size() < 4 || (argv.size() % 2) != 0)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* hash = lookupHash(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    long long added = 0;
    for (size_t i = 2; i < argv.size(); i += 2)
    {
        bool field_added = false;
        hashSet(hash, argv[i], argv[i + 1], field_added);
        if (field_added)
        {
            ++added;
        }
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

    string value;
    if (!hashGet(it->second, argv[2], value))
    {
        return encodeNullBulk();
    }

    return encodeBulkString(value);
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

    string reply = "*" + to_string(argv.size() - 2) + "\r\n";
    for (size_t i = 2; i < argv.size(); ++i)
    {
        string value;
        if (hashGet(it->second, argv[i], value))
        {
            reply += encodeBulkString(value);
        }
        else
        {
            reply += encodeNullBulk();
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
    RedisObject* hash = lookupHash(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (hash == nullptr)
    {
        return encodeInteger(0);
    }

    return encodeInteger(hashDel(hash, vector<string>(argv.begin() + 2, argv.end())));
}

string commandHExists(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 3)
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

    return encodeInteger(hashExists(it->second, argv[2]) ? 1 : 0);
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

    return encodeInteger(hashLen(it->second));
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

    return encodeArray(hashKeys(it->second));
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

    return encodeArray(hashVals(it->second));
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

    return encodeArray(hashGetAllFlat(it->second));
}

string commandHIncrBy(const vector<string>& argv, Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* hash = lookupHash(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    long long delta = stoll(argv[3]);
    long long next = 0;
    if (!hashIncrBy(hash, argv[2], delta, next))
    {
        return notAnInteger();
    }

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
