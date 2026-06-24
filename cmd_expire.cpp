#include "cmd_expire.h"

#include "commands.h"
#include "resp.h"

using namespace std;

namespace
{
string wrongArity(const string& command)
{
    return encodeError("ERR wrong number of arguments for '" + command + "' command");
}
}

string dispatchExpireCommand(const vector<string>& argv, RedisDb& db)
{
    const string& cmd = argv[0];

    if (cmd == "EXPIRE")
    {
        if (argv.size() != 3)
        {
            return wrongArity(cmd);
        }

        long long seconds = stoll(argv[2]);
        long long expire_ms = nowMs() + seconds * 1000;
        return encodeInteger(setExpireAtMs(db, argv[1], expire_ms) ? 1 : 0);
    }

    if (cmd == "PEXPIRE")
    {
        if (argv.size() != 3)
        {
            return wrongArity(cmd);
        }

        long long ms = stoll(argv[2]);
        long long expire_ms = nowMs() + ms;
        return encodeInteger(setExpireAtMs(db, argv[1], expire_ms) ? 1 : 0);
    }

    if (cmd == "EXPIREAT")
    {
        if (argv.size() != 3)
        {
            return wrongArity(cmd);
        }

        long long expire_ms = stoll(argv[2]) * 1000;
        return encodeInteger(setExpireAtMs(db, argv[1], expire_ms) ? 1 : 0);
    }

    if (cmd == "PEXPIREAT")
    {
        if (argv.size() != 3)
        {
            return wrongArity(cmd);
        }

        long long expire_ms = stoll(argv[2]);
        return encodeInteger(setExpireAtMs(db, argv[1], expire_ms) ? 1 : 0);
    }

    if (cmd == "TTL")
    {
        if (argv.size() != 2)
        {
            return wrongArity(cmd);
        }

        return encodeInteger(ttlSeconds(db, argv[1]));
    }

    if (cmd == "PTTL")
    {
        if (argv.size() != 2)
        {
            return wrongArity(cmd);
        }

        return encodeInteger(ttlMilliseconds(db, argv[1]));
    }

    if (cmd == "PERSIST")
    {
        if (argv.size() != 2)
        {
            return wrongArity(cmd);
        }

        if (!keyExists(db, argv[1]))
        {
            return encodeInteger(0);
        }

        return encodeInteger(removeExpire(db, argv[1]) ? 1 : 0);
    }

    return encodeError("ERR unknown command");
}

void registerExpireCommands(CommandTable& table)
{
    auto run = [&](const char* name, int arity, uint32_t flags) {
        table[name] = Command{
            name,
            [](CommandContext& ctx, const vector<string>& argv) {
                return dispatchExpireCommand(argv, ctx.db());
            },
            arity,
            flags};
    };

    run("EXPIRE", 3, CMD_WRITE);
    run("PEXPIRE", 3, CMD_WRITE);
    run("EXPIREAT", 3, CMD_WRITE);
    run("PEXPIREAT", 3, CMD_WRITE);
    run("TTL", 2, CMD_READONLY);
    run("PTTL", 2, CMD_READONLY);
    run("PERSIST", 2, CMD_WRITE);
}
