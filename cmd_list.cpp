#include "cmd_list.h"

#include "commands.h"

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

RedisObject* lookupList(Db& db, const string& key, bool create, bool& type_error)
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
        return obj;
    }

    if (it->second->type != OBJ_LIST)
    {
        type_error = true;
        return nullptr;
    }

    return it->second;
}

string commandLPush(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* list = lookupList(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    listPushFront(list, vector<string>(argv.begin() + 2, argv.end()));
    return encodeInteger(listLen(list));
}

string commandRPush(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* list = lookupList(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    listPushBack(list, vector<string>(argv.begin() + 2, argv.end()));
    return encodeInteger(listLen(list));
}

string popReply(const vector<string>& popped, bool multi)
{
    if (multi)
    {
        return encodeArray(popped);
    }

    if (popped.empty())
    {
        return encodeNullBulk();
    }

    return encodeBulkString(popped.front());
}

string commandLPop(const vector<string>& argv, Db& db)
{
    if (argv.size() < 2 || argv.size() > 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* list = lookupList(db, argv[1], false, type_error);
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

    if (list == nullptr || listLen(list) == 0)
    {
        return popReply({}, argv.size() == 3);
    }

    return popReply(listPop(list, true, count), argv.size() == 3);
}

string commandRPop(const vector<string>& argv, Db& db)
{
    if (argv.size() < 2 || argv.size() > 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* list = lookupList(db, argv[1], false, type_error);
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

    if (list == nullptr || listLen(list) == 0)
    {
        return popReply({}, argv.size() == 3);
    }

    return popReply(listPop(list, false, count), argv.size() == 3);
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

    return encodeInteger(listLen(it->second));
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

    return encodeArray(listRange(it->second, stoll(argv[2]), stoll(argv[3])));
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

    string value;
    if (!listIndex(it->second, stoll(argv[2]), value))
    {
        return encodeNullBulk();
    }

    return encodeBulkString(value);
}

string commandLSet(const vector<string>& argv, Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (list == nullptr)
    {
        return encodeError("ERR no such key");
    }

    const long long size = listLen(list);
    const long long index = normalizeIndex(stoll(argv[2]), size);
    if (!indexInRange(index, size))
    {
        return encodeError("ERR index out of range");
    }

    if (!listSet(list, index, argv[3]))
    {
        return encodeError("ERR index out of range");
    }

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
    RedisObject* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (list == nullptr)
    {
        return encodeInteger(0);
    }

    return encodeInteger(listInsert(list, where == "BEFORE", argv[3], argv[4]));
}

string commandLRem(const vector<string>& argv, Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (list == nullptr)
    {
        return encodeInteger(0);
    }

    return encodeInteger(listRem(list, stoll(argv[2]), argv[3]));
}

string commandLTrim(const vector<string>& argv, Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* list = lookupList(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (list != nullptr)
    {
        listTrim(list, stoll(argv[2]), stoll(argv[3]));
    }

    return encodeOK();
}
}

void registerListCommands(CommandTable& table)
{
    auto run = [&](const char* name, int arity, uint32_t flags, auto handler) {
        table[name] = Command{name, handler, arity, flags};
    };

    run("LPUSH", -3, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandLPush(argv, ctx.db().data);
    });
    run("RPUSH", -3, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandRPush(argv, ctx.db().data);
    });
    run("LPOP", -2, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandLPop(argv, ctx.db().data);
    });
    run("RPOP", -2, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandRPop(argv, ctx.db().data);
    });
    run("LLEN", 2, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandLLen(argv, ctx.db().data);
    });
    run("LRANGE", 4, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandLRange(argv, ctx.db().data);
    });
    run("LINDEX", 3, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandLIndex(argv, ctx.db().data);
    });
    run("LSET", 4, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandLSet(argv, ctx.db().data);
    });
    run("LINSERT", 5, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandLInsert(argv, ctx.db().data);
    });
    run("LREM", 4, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandLRem(argv, ctx.db().data);
    });
    run("LTRIM", 4, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandLTrim(argv, ctx.db().data);
    });
}
