#include "cmd_string.h"

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
    return encodeError("ERR value is not an integer or out of range");
}

void putString(Db& db, const string& key, const string& value)
{
    auto it = db.find(key);
    if (it != db.end())
    {
        destroyObject(it->second);
        db.erase(it);
    }

    db[key] = createStringObject(value);
}

RedisObject* ensureStringKey(Db& db, const string& key)
{
    auto it = db.find(key);
    if (it == db.end())
    {
        db[key] = createStringObject("0");
        return db[key];
    }

    if (it->second->type != OBJ_STRING)
    {
        return nullptr;
    }

    return it->second;
}

string getStringOrNil(const Db& db, const string& key)
{
    auto it = db.find(key);
    if (it == db.end())
    {
        return encodeNullBulk();
    }

    if (it->second->type != OBJ_STRING)
    {
        return wrongType();
    }

    return encodeBulkString(getStringValue(it->second));
}

struct SetOptions
{
    bool nx = false;
    bool xx = false;
    bool has_ex = false;
    bool has_px = false;
    bool has_exat = false;
    bool has_pxat = false;
    long long ex = 0;
    long long px = 0;
    long long exat = 0;
    long long pxat = 0;
};

bool parseSetOptions(const vector<string>& argv, SetOptions& opts, string& err)
{
    for (size_t i = 3; i < argv.size(); ++i)
    {
        const string& opt = argv[i];

        if (opt == "NX")
        {
            opts.nx = true;
            continue;
        }

        if (opt == "XX")
        {
            opts.xx = true;
            continue;
        }

        if (opt == "EX")
        {
            if (i + 1 >= argv.size())
            {
                err = "syntax error";
                return false;
            }

            opts.has_ex = true;
            opts.ex = stoll(argv[++i]);
            continue;
        }

        if (opt == "PX")
        {
            if (i + 1 >= argv.size())
            {
                err = "syntax error";
                return false;
            }

            opts.has_px = true;
            opts.px = stoll(argv[++i]);
            continue;
        }

        if (opt == "EXAT")
        {
            if (i + 1 >= argv.size())
            {
                err = "syntax error";
                return false;
            }

            opts.has_exat = true;
            opts.exat = stoll(argv[++i]);
            continue;
        }

        if (opt == "PXAT")
        {
            if (i + 1 >= argv.size())
            {
                err = "syntax error";
                return false;
            }

            opts.has_pxat = true;
            opts.pxat = stoll(argv[++i]);
            continue;
        }

        err = "syntax error";
        return false;
    }

    if (opts.nx && opts.xx)
    {
        err = "syntax error";
        return false;
    }

    int ttl_opts = (opts.has_ex ? 1 : 0) + (opts.has_px ? 1 : 0)
        + (opts.has_exat ? 1 : 0) + (opts.has_pxat ? 1 : 0);
    if (ttl_opts > 1)
    {
        err = "syntax error";
        return false;
    }

    return true;
}

void applySetExpiry(RedisDb& db, const string& key, const SetOptions& opts)
{
    if (opts.has_ex)
    {
        db.expires[key] = nowMs() + opts.ex * 1000;
        return;
    }

    if (opts.has_px)
    {
        db.expires[key] = nowMs() + opts.px;
        return;
    }

    if (opts.has_exat)
    {
        db.expires[key] = opts.exat * 1000;
        return;
    }

    if (opts.has_pxat)
    {
        db.expires[key] = opts.pxat;
        return;
    }

    db.expires.erase(key);
}

bool canSetKey(const Db& db, const string& key, const SetOptions& opts)
{
    const bool exists = db.find(key) != db.end();

    if (opts.nx && exists)
    {
        return false;
    }

    if (opts.xx && !exists)
    {
        return false;
    }

    return true;
}

string commandSet(const vector<string>& argv, RedisDb& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    SetOptions opts;
    string err;
    if (!parseSetOptions(argv, opts, err))
    {
        return encodeError("ERR " + err);
    }

    if (!canSetKey(db.data, argv[1], opts))
    {
        return encodeNullBulk();
    }

    putString(db.data, argv[1], argv[2]);
    applySetExpiry(db, argv[1], opts);
    return encodeOK();
}

string commandGet(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    return getStringOrNil(db, argv[1]);
}

string commandGetSet(const vector<string>& argv, Db& db)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }

    string reply = getStringOrNil(db, argv[1]);
    if (reply.rfind("-WRONGTYPE", 0) == 0)
    {
        return reply;
    }

    putString(db, argv[1], argv[2]);
    return reply;
}

string commandMSet(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3 || (argv.size() % 2) == 0)
    {
        return wrongArity(argv[0]);
    }

    for (size_t i = 1; i < argv.size(); i += 2)
    {
        putString(db, argv[i], argv[i + 1]);
    }

    return encodeOK();
}

string commandMGet(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    string reply = "*" + to_string(argv.size() - 1) + "\r\n";

    for (size_t i = 1; i < argv.size(); ++i)
    {
        auto it = db.find(argv[i]);
        if (it == db.end())
        {
            reply += encodeNullBulk();
            continue;
        }

        if (it->second->type != OBJ_STRING)
        {
            return wrongType();
        }

        reply += encodeBulkString(getStringValue(it->second));
    }

    return reply;
}

string commandSetNx(const vector<string>& argv, Db& db)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }

    if (db.find(argv[1]) != db.end())
    {
        return encodeInteger(0);
    }

    putString(db, argv[1], argv[2]);
    return encodeInteger(1);
}

string commandSetEx(const vector<string>& argv, RedisDb& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    long long seconds = stoll(argv[2]);
    putString(db.data, argv[1], argv[3]);
    db.expires[argv[1]] = nowMs() + seconds * 1000;
    return encodeOK();
}

string adjustInteger(Db& db, const string& key, long long delta)
{
    RedisObject* obj = ensureStringKey(db, key);
    if (obj == nullptr)
    {
        return wrongType();
    }

    long long current = 0;
    if (!readStringInteger(obj, current))
    {
        return notAnInteger();
    }

    long long next = current + delta;
    setStringInteger(obj, next);
    return encodeInteger(next);
}

string commandIncrBy(const vector<string>& argv, Db& db, long long delta)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    return adjustInteger(db, argv[1], delta);
}

string commandAppend(const vector<string>& argv, Db& db)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end())
    {
        putString(db, argv[1], argv[2]);
        return encodeInteger(static_cast<long long>(argv[2].size()));
    }

    if (it->second->type != OBJ_STRING)
    {
        return wrongType();
    }

    appendStringValue(it->second, argv[2]);
    return encodeInteger(static_cast<long long>(stringObjectLength(it->second)));
}

string commandStrLen(const vector<string>& argv, const Db& db)
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

    if (it->second->type != OBJ_STRING)
    {
        return wrongType();
    }

    return encodeInteger(static_cast<long long>(stringObjectLength(it->second)));
}
}

string dispatchStringCommand(const vector<string>& argv, RedisDb& db)
{
    const string& cmd = argv[0];

    if (cmd == "SET")
    {
        return commandSet(argv, db);
    }

    if (cmd == "GET")
    {
        return commandGet(argv, db.data);
    }

    if (cmd == "GETSET")
    {
        return commandGetSet(argv, db.data);
    }

    if (cmd == "MSET")
    {
        return commandMSet(argv, db.data);
    }

    if (cmd == "MGET")
    {
        return commandMGet(argv, db.data);
    }

    if (cmd == "SETNX")
    {
        return commandSetNx(argv, db.data);
    }

    if (cmd == "SETEX")
    {
        return commandSetEx(argv, db);
    }

    if (cmd == "INCR")
    {
        return commandIncrBy(argv, db.data, 1);
    }

    if (cmd == "DECR")
    {
        return commandIncrBy(argv, db.data, -1);
    }

    if (cmd == "INCRBY")
    {
        if (argv.size() != 3)
        {
            return wrongArity(argv[0]);
        }

        return adjustInteger(db.data, argv[1], stoll(argv[2]));
    }

    if (cmd == "DECRBY")
    {
        if (argv.size() != 3)
        {
            return wrongArity(argv[0]);
        }

        return adjustInteger(db.data, argv[1], -stoll(argv[2]));
    }

    if (cmd == "APPEND")
    {
        return commandAppend(argv, db.data);
    }

    if (cmd == "STRLEN")
    {
        return commandStrLen(argv, db.data);
    }

    return encodeError("ERR unknown command");
}
