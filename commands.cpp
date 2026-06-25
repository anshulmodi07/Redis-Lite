#include "commands.h"

#include "cmd_expire.h"
#include "eviction.h"
#include "cmd_hash.h"
#include "cmd_list.h"
#include "cmd_set.h"
#include "cmd_string.h"
#include "cmd_zset.h"
#include "cluster.h"
#include "encoding.h"
#include "aof.h"
#include "eviction.h"
#include "multi.h"
#include "pubsub.h"
#include "rdb.h"
#include "replication.h"
#include "scripting.h"
#include "object.h"
#include "resp.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <thread>
#include <vector>
#include <sys/utsname.h>
#include <iomanip>
#include <sstream>
#include <fstream>

using namespace std;

ServerStats g_stats;

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

string commandConfig(CommandContext& ctx, const vector<string>& argv)
{
    string sub = argv[1];
    transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char ch) {
        return static_cast<char>(toupper(ch));
    });

    if (sub == "GET")
    {
        if (argv.size() != 3)
        {
            return wrongArity("CONFIG");
        }

        const string& key = argv[2];
        if (key == "*")
        {
            return encodeArray({
                "maxmemory", to_string(g_server_config.maxmemory),
                "maxmemory-policy", evictionPolicyName(g_server_config.maxmemory_policy),
                "maxmemory-samples", to_string(g_server_config.maxmemory_samples),
                "appendfsync", aofFsyncPolicyName(g_aof_fsync_policy)
            });
        }

        if (key == "maxmemory")
        {
            return encodeArray({key, to_string(g_server_config.maxmemory)});
        }

        if (key == "maxmemory-policy")
        {
            return encodeArray({key, evictionPolicyName(g_server_config.maxmemory_policy)});
        }

        if (key == "maxmemory-samples")
        {
            return encodeArray({key, to_string(g_server_config.maxmemory_samples)});
        }

        if (key == "appendfsync")
        {
            return encodeArray({key, aofFsyncPolicyName(g_aof_fsync_policy)});
        }

        return encodeError("ERR unknown configuration parameter");
    }

    if (sub == "SET")
    {
        if (argv.size() != 4)
        {
            return wrongArity("CONFIG");
        }

        const string& key = argv[2];
        const string& value = argv[3];
        if (key == "maxmemory")
        {
            size_t bytes = 0;
            if (!parseMemoryBytes(value, bytes))
            {
                return encodeError("ERR invalid maxmemory value");
            }

            g_server_config.maxmemory = bytes;
            string err = ensureMemoryForWrite(ctx.databases);
            return err.empty() ? encodeOK() : err;
        }

        if (key == "maxmemory-policy")
        {
            EvictionPolicy policy = EvictionPolicy::NoEviction;
            if (!parseEvictionPolicy(value, policy))
            {
                return encodeError("ERR unsupported maxmemory policy");
            }

            g_server_config.maxmemory_policy = policy;
            return encodeOK();
        }

        if (key == "maxmemory-samples")
        {
            long long samples = 0;
            try
            {
                samples = stoll(value);
            }
            catch (...)
            {
                return encodeError("ERR invalid maxmemory samples");
            }

            if (samples <= 0)
            {
                return encodeError("ERR invalid maxmemory samples");
            }

            g_server_config.maxmemory_samples = static_cast<size_t>(samples);
            return encodeOK();
        }

        if (key == "appendfsync")
        {
            AofFsyncPolicy policy = AofFsyncPolicy::EverySec;
            if (!parseAofFsyncPolicy(value, policy))
            {
                return encodeError("ERR invalid appendfsync value");
            }

            g_aof_fsync_policy = policy;
            return encodeOK();
        }

        return encodeError("ERR unsupported CONFIG parameter");
    }

    return wrongArity("CONFIG");
}

string commandSave(CommandContext& ctx, const vector<string>&)
{
    if (!saveRDB(g_rdb_filename, ctx.databases))
    {
        return encodeError("ERR rdb save failed");
    }

    return encodeOK();
}

string commandBgsave(CommandContext& ctx, const vector<string>&)
{
    if (bgsaveInProgress() || bgrewriteInProgress())
    {
        return encodeError("ERR Background save already in progress");
    }

    if (!startBgsave(ctx.databases))
    {
        return encodeError("bgsave failed");
    }

    return encodeSimpleString("Background saving started");
}

string commandBgrewriteAof(CommandContext& ctx, const vector<string>&)
{
    if (bgsaveInProgress() || bgrewriteInProgress())
    {
        return encodeError("ERR Background append only file rewriting already in progress");
    }

    if (!startBgrewrite(ctx.databases))
    {
        return encodeError("ERR bgrewriteaof failed");
    }

    return encodeSimpleString("Background append only file rewriting started");
}

string commandInfo(CommandContext& ctx, const vector<string>& argv)
{
    string section;
    if (argv.size() >= 2)
    {
        section = argv[1];
        for (char& ch : section)
        {
            ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        }
    }

    string body;

    if (section.empty() || section == "server")
    {
        body += "# Server\r\n";
        body += "redis_version:5.0-lite\r\n";
        string mode = g_server_config.cluster_enabled ? "cluster" : "standalone";
        body += "redis_mode:" + mode + "\r\n";
        body += "os:";
        utsname name;
        if (uname(&name) == 0)
        {
            body += string(name.sysname) + " " + name.release + " " + name.machine + "\r\n";
        }
        else
        {
            body += "Linux\r\n";
        }
        body += "tcp_port:" + to_string(g_server_config.port) + "\r\n";
        long long uptime_secs = 0;
        if (g_stats.start_time_ms > 0)
        {
            uptime_secs = (nowMs() - g_stats.start_time_ms) / 1000;
        }
        body += "uptime_in_seconds:" + to_string(uptime_secs) + "\r\n";
        body += "uptime_in_days:" + to_string(uptime_secs / 86400) + "\r\n";
    }

    if (section.empty() || section == "clients")
    {
        body += "# Clients\r\n";
        size_t connected = ctx.clients ? ctx.clients->size() : 0;
        body += "connected_clients:" + to_string(connected) + "\r\n";
        body += "blocked_clients:0\r\n";
    }

    if (section.empty() || section == "memory")
    {
        body += "# Memory\r\n";
        size_t used = estimateServerMemory(ctx.databases);
        body += "used_memory:" + to_string(used) + "\r\n";
        
        size_t rss = 0;
        ifstream status_file("/proc/self/status");
        string line;
        while (getline(status_file, line))
        {
            if (line.rfind("VmRSS:", 0) == 0)
            {
                stringstream ss(line.substr(6));
                ss >> rss; // VmRSS is in kB
                rss *= 1024; // Convert to bytes
                break;
            }
        }
        double frag_ratio = 1.00;
        if (used > 0 && rss > 0)
        {
            frag_ratio = static_cast<double>(rss) / used;
        }
        stringstream frag_ss;
        frag_ss << fixed << setprecision(2) << frag_ratio;
        body += "mem_fragmentation_ratio:" + frag_ss.str() + "\r\n";
    }

    if (section.empty() || section == "stats")
    {
        body += "# Stats\r\n";
        body += "total_commands_processed:" + to_string(g_stats.total_commands_processed) + "\r\n";
        body += "total_connections_received:" + to_string(g_stats.total_connections_received) + "\r\n";
        body += "instantaneous_ops_per_sec:" + to_string(g_stats.ops_per_sec) + "\r\n";
        body += "ops_per_sec:" + to_string(g_stats.ops_per_sec) + "\r\n";
    }

    if (section.empty() || section == "replication")
    {
        body += replicationInfoSection();
    }

    if (section.empty() || section == "cluster")
    {
        body += clusterInfoSection();
    }

    if (section.empty() || section == "keyspace")
    {
        body += "# Keyspace\r\n";
        for (size_t i = 0; i < ctx.databases.size(); ++i)
        {
            const RedisDb& db = ctx.databases[i];
            if (db.data.empty() && db.expires.empty())
            {
                continue;
            }

            body += "db" + to_string(i) + ":keys=" + to_string(db.data.size())
                + ",expires=" + to_string(db.expires.size()) + ",avg_ttl=0\r\n";
        }
    }

    return encodeBulkString(body);
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
    add(out, "CONFIG", -3, CMD_READONLY, commandConfig);
    add(out, "SAVE", 1, CMD_READONLY, commandSave);
    add(out, "BGSAVE", 1, CMD_READONLY, commandBgsave);
    add(out, "BGREWRITEAOF", 1, CMD_READONLY, commandBgrewriteAof);
    add(out, "INFO", -1, CMD_READONLY, commandInfo);
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
    registerPubsubCommands(table);
    registerScriptingCommands(table);
    registerClusterCommands(table);
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

    ++g_stats.total_commands_processed;

    string repl_reply;
    if (replicationHandleCommand(ctx.client, argv, repl_reply))
    {
        return repl_reply;
    }

    string transaction_reply;
    if (tryTransaction(ctx, argv, transaction_reply))
    {
        return transaction_reply;
    }

    if (ctx.client.pubsub_mode && !pubsubAllowsInMode(argv[0]))
    {
        return encodeError("ERR only (P)SUBSCRIBE / (P)UNSUBSCRIBE / PING allowed in subscribed state");
    }

    const string cluster_err = clusterPreflight(argv);
    if (!cluster_err.empty())
    {
        return cluster_err;
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

    string readonly_err;
    if ((cmd.flags & CMD_WRITE) != 0 && !replicationPreflightWrite(ctx.client, readonly_err))
    {
        return readonly_err;
    }

    if ((cmd.flags & CMD_WRITE) != 0)
    {
        string oom = ensureMemoryForWrite(ctx.databases);
        if (!oom.empty())
        {
            return oom;
        }
    }

    string result = cmd.func(ctx, argv);
    if ((cmd.flags & CMD_WRITE) != 0 && !ctx.exec_replay)
    {
        notifyWriteKeys(ctx, argv, cmd.flags);
        if (argv[0] != "EVAL" && argv[0] != "EVALSHA")
        {
            aofAppendCommand(argv);
            replicationFeedWrite(argv);
        }
    }

    return result;
}
