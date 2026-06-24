#include "commands.h"

#include "cmd_expire.h"
#include "cmd_hash.h"
#include "cmd_list.h"
#include "cmd_set.h"
#include "cmd_string.h"
#include "cmd_zset.h"
#include "encoding.h"
#include "object.h"
#include "resp.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <thread>
#include <vector>

using namespace std;

namespace
{
constexpr int DB_COUNT = 16;

CommandTable table;

string wrongArity(const string& command)
{
    return encodeError("ERR wrong number of arguments for '" + command + "' command");
}

bool arityOk(const Command& cmd, size_t argc)
{
    if (cmd.arity > 0)
    {
        return argc == static_cast<size_t>(cmd.arity);
    }

    return argc >= static_cast<size_t>(-cmd.arity);
}

void flushDb(RedisDb& db)
{
    for (auto& item : db.data)
    {
        destroyObject(item.second);
    }

    db.data.clear();
    db.expires.clear();
}

bool globMatch(const string& pattern, const string& text)
{
    size_t pi = 0;
    size_t ti = 0;
    size_t star_pi = string::npos;
    size_t star_ti = 0;

    while (ti < text.size())
    {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti]))
        {
            ++pi;
            ++ti;
        }
        else if (pi < pattern.size() && pattern[pi] == '*')
        {
            star_pi = pi++;
            star_ti = ti;
        }
        else if (star_pi != string::npos)
        {
            pi = star_pi + 1;
            ti = ++star_ti;
        }
        else
        {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*')
    {
        ++pi;
    }

    return pi == pattern.size();
}

vector<string> sortedKeys(const RedisDb& db)
{
    vector<string> keys;
    keys.reserve(db.data.size());
    for (const auto& item : db.data)
    {
        keys.push_back(item.first);
    }

    sort(keys.begin(), keys.end());
    return keys;
}

void add(CommandTable& out, const char* name, int arity, uint32_t flags, CommandFunc func)
{
    out[name] = Command{name, std::move(func), arity, flags};
}

string commandPing(CommandContext&, const vector<string>& argv)
{
    if (argv.size() == 1)
    {
        return encodeSimpleString("PONG");
    }

    if (argv.size() == 2)
    {
        return encodeBulkString(argv[1]);
    }

    return wrongArity(argv[0]);
}

string commandEcho(CommandContext&, const vector<string>& argv)
{
    return encodeBulkString(argv[1]);
}

string commandSelect(CommandContext& ctx, const vector<string>& argv)
{
    long long index = 0;
    try
    {
        index = stoll(argv[1]);
    }
    catch (...)
    {
        return encodeError("ERR invalid DB index");
    }

    if (index < 0 || index >= DB_COUNT)
    {
        return encodeError("ERR DB index is out of range");
    }

    ctx.client.db_index = static_cast<int>(index);
    return encodeOK();
}

string commandDbSize(CommandContext& ctx, const vector<string>&)
{
    return encodeInteger(static_cast<long long>(ctx.db().data.size()));
}

string commandFlushDb(CommandContext& ctx, const vector<string>&)
{
    flushDb(ctx.db());
    return encodeOK();
}

string commandFlushAll(CommandContext& ctx, const vector<string>&)
{
    for (RedisDb& db : ctx.databases)
    {
        flushDb(db);
    }

    return encodeOK();
}

string commandKeys(CommandContext& ctx, const vector<string>& argv)
{
    vector<string> matches;
    for (const string& key : sortedKeys(ctx.db()))
    {
        if (globMatch(argv[1], key))
        {
            matches.push_back(key);
        }
    }

    return encodeArray(matches);
}

string commandScan(CommandContext& ctx, const vector<string>& argv)
{
    size_t cursor = 0;
    try
    {
        cursor = static_cast<size_t>(stoull(argv[1]));
    }
    catch (...)
    {
        return encodeError("ERR invalid cursor");
    }

    string pattern = "*";
    size_t count = 10;
    for (size_t i = 2; i < argv.size(); ++i)
    {
        if (argv[i] == "MATCH" && i + 1 < argv.size())
        {
            pattern = argv[++i];
        }
        else if (argv[i] == "COUNT" && i + 1 < argv.size())
        {
            count = static_cast<size_t>(max(1LL, stoll(argv[++i])));
        }
        else
        {
            return wrongArity(argv[0]);
        }
    }

    vector<string> keys;
    for (const string& key : sortedKeys(ctx.db()))
    {
        if (globMatch(pattern, key))
        {
            keys.push_back(key);
        }
    }

    size_t next_cursor = 0;
    vector<string> batch;
    if (cursor < keys.size())
    {
        size_t end = min(cursor + count, keys.size());
        batch.assign(keys.begin() + static_cast<ptrdiff_t>(cursor), keys.begin() + static_cast<ptrdiff_t>(end));
        next_cursor = end < keys.size() ? end : 0;
    }

    string reply = "*2\r\n";
    reply += encodeBulkString(to_string(next_cursor));
    reply += encodeArray(batch);
    return reply;
}

string commandExists(CommandContext& ctx, const vector<string>& argv)
{
    long long count = 0;
    for (size_t i = 1; i < argv.size(); ++i)
    {
        if (ctx.db().data.find(argv[i]) != ctx.db().data.end())
        {
            ++count;
        }
    }

    return encodeInteger(count);
}

string commandType(CommandContext& ctx, const vector<string>& argv)
{
    auto it = ctx.db().data.find(argv[1]);
    if (it == ctx.db().data.end())
    {
        return encodeSimpleString("none");
    }

    return encodeSimpleString(objectTypeName(it->second->type));
}

string commandDel(CommandContext& ctx, const vector<string>& argv)
{
    long long removed = 0;
    RedisDb& db = ctx.db();
    for (size_t i = 1; i < argv.size(); ++i)
    {
        auto it = db.data.find(argv[i]);
        if (it != db.data.end())
        {
            destroyObject(it->second);
            db.data.erase(it);
            db.expires.erase(argv[i]);
            ++removed;
        }
    }

    return encodeInteger(removed);
}

string commandRename(CommandContext& ctx, const vector<string>& argv)
{
    RedisDb& db = ctx.db();
    const string& src = argv[1];
    const string& dst = argv[2];

    if (src == dst)
    {
        return encodeOK();
    }

    auto src_it = db.data.find(src);
    if (src_it == db.data.end())
    {
        return encodeError("ERR no such key");
    }

    auto dst_it = db.data.find(dst);
    if (dst_it != db.data.end())
    {
        destroyObject(dst_it->second);
        db.data.erase(dst_it);
        db.expires.erase(dst);
    }

    RedisObject* obj = src_it->second;
    db.data.erase(src_it);
    db.data.emplace(dst, obj);

    auto exp_it = db.expires.find(src);
    if (exp_it != db.expires.end())
    {
        db.expires[dst] = exp_it->second;
        db.expires.erase(exp_it);
    }

    return encodeOK();
}

string commandRenameNx(CommandContext& ctx, const vector<string>& argv)
{
    RedisDb& db = ctx.db();
    const string& src = argv[1];
    const string& dst = argv[2];

    if (src == dst || db.data.find(src) == db.data.end())
    {
        if (db.data.find(src) == db.data.end())
        {
            return encodeError("ERR no such key");
        }

        return encodeInteger(0);
    }

    if (db.data.find(dst) != db.data.end())
    {
        return encodeInteger(0);
    }

    RedisObject* obj = db.data[src];
    db.data.erase(src);
    db.data.emplace(dst, obj);

    auto exp_it = db.expires.find(src);
    if (exp_it != db.expires.end())
    {
        db.expires[dst] = exp_it->second;
        db.expires.erase(exp_it);
    }

    return encodeInteger(1);
}

string commandObjectEncoding(CommandContext& ctx, const vector<string>& argv)
{
    string sub = argv[1];
    transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char ch) {
        return static_cast<char>(toupper(ch));
    });

    if (sub != "ENCODING")
    {
        return wrongArity("OBJECT");
    }

    auto it = ctx.db().data.find(argv[2]);
    if (it == ctx.db().data.end())
    {
        return encodeNullBulk();
    }

    return encodeBulkString(objectEncodingName(it->second));
}

string commandDebugSleep(CommandContext&, const vector<string>& argv)
{
    string sub = argv[1];
    transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char ch) {
        return static_cast<char>(toupper(ch));
    });

    if (sub != "SLEEP")
    {
        return wrongArity("DEBUG");
    }

    double seconds = 0.0;
    try
    {
        seconds = stod(argv[2]);
    }
    catch (...)
    {
        return encodeError("ERR invalid sleep time");
    }

    if (seconds < 0.0)
    {
        return encodeError("ERR invalid sleep time");
    }

    this_thread::sleep_for(chrono::duration<double>(seconds));
    return encodeOK();
}

void registerUtilityCommands(CommandTable& out)
{
    add(out, "PING", -1, CMD_READONLY, commandPing);
    add(out, "ECHO", 2, CMD_READONLY, commandEcho);
    add(out, "SELECT", 2, CMD_READONLY, commandSelect);
    add(out, "DBSIZE", 1, CMD_READONLY, commandDbSize);
    add(out, "FLUSHDB", 1, CMD_WRITE, commandFlushDb);
    add(out, "FLUSHALL", 1, CMD_WRITE, commandFlushAll);
    add(out, "KEYS", 2, CMD_READONLY, commandKeys);
    add(out, "SCAN", -2, CMD_READONLY, commandScan);
    add(out, "EXISTS", -2, CMD_READONLY, commandExists);
    add(out, "TYPE", 2, CMD_READONLY, commandType);
    add(out, "DEL", -2, CMD_WRITE, commandDel);
    add(out, "RENAME", 3, CMD_WRITE, commandRename);
    add(out, "RENAMENX", 3, CMD_WRITE, commandRenameNx);
    add(out, "OBJECT", 3, CMD_READONLY, commandObjectEncoding);
    add(out, "DEBUG", 3, CMD_READONLY, commandDebugSleep);
}
}

void initCommandTable()
{
    table.clear();
    registerUtilityCommands(table);
    registerStringCommands(table);
    registerExpireCommands(table);
    registerHashCommands(table);
    registerListCommands(table);
    registerSetCommands(table);
    registerZSetCommands(table);
}

const CommandTable& commandTable()
{
    return table;
}

string executeCommand(CommandContext& ctx, const vector<string>& argv)
{
    if (argv.empty())
    {
        return encodeError("ERR unknown command");
    }

    auto it = table.find(argv[0]);
    if (it == table.end())
    {
        if (argv[0] == "OBJECT")
        {
            return wrongArity("OBJECT");
        }

        return encodeError("ERR unknown command");
    }

    const Command& cmd = it->second;
    if (!arityOk(cmd, argv.size()))
    {
        return wrongArity(argv[0]);
    }

    return cmd.func(ctx, argv);
}
