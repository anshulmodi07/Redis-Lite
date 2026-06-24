#include "multi.h"

#include "parser.h"
#include "pubsub.h"
#include "resp.h"

#include <functional>

using namespace std;

namespace
{
unordered_map<string, unordered_set<int>> watched_keys;

string watchKey(int db_index, const string& key)
{
    return to_string(db_index) + "|" + key;
}

bool arityOk(const Command& cmd, size_t argc)
{
    if (cmd.arity > 0)
    {
        return argc == static_cast<size_t>(cmd.arity);
    }

    return argc >= static_cast<size_t>(-cmd.arity);
}

string wrongArity(const string& command)
{
    return encodeError("ERR wrong number of arguments for '" + command + "' command");
}

void clearMultiState(Client& client)
{
    client.in_multi = false;
    client.multi_error = false;
    client.queued_commands.clear();
}

void clearClientWatches(Client& client)
{
    for (const string& wkey : client.watches)
    {
        auto it = watched_keys.find(wkey);
        if (it != watched_keys.end())
        {
            it->second.erase(client.fd);
            if (it->second.empty())
            {
                watched_keys.erase(it);
            }
        }
    }

    client.watches.clear();
    client.dirty = false;
}

void markDirty(int watcher_fd, unordered_map<int, Client>& clients)
{
    auto it = clients.find(watcher_fd);
    if (it != clients.end())
    {
        it->second.dirty = true;
    }
}

void notifyKeysModified(CommandContext& ctx, const vector<string>& keys)
{
    if (!ctx.clients)
    {
        return;
    }

    const int writer_fd = ctx.client.fd;
    const int db_index = ctx.client.db_index;

    for (const string& key : keys)
    {
        auto it = watched_keys.find(watchKey(db_index, key));
        if (it == watched_keys.end())
        {
            continue;
        }

        for (int watcher_fd : it->second)
        {
            if (watcher_fd != writer_fd)
            {
                markDirty(watcher_fd, *ctx.clients);
            }
        }
    }
}

void notifyDbFlushed(CommandContext& ctx, int db_index, bool all_dbs)
{
    if (!ctx.clients)
    {
        return;
    }

    const int writer_fd = ctx.client.fd;
    const string prefix = to_string(db_index) + "|";

    for (const auto& entry : watched_keys)
    {
        if (!all_dbs && entry.first.compare(0, prefix.size(), prefix) != 0)
        {
            continue;
        }

        for (int watcher_fd : entry.second)
        {
            if (watcher_fd != writer_fd)
            {
                markDirty(watcher_fd, *ctx.clients);
            }
        }
    }
}

string validateQueuedCommand(const Client& client, const vector<string>& argv, const CommandTable& table)
{
    if (client.pubsub_mode && !pubsubAllowsInMode(argv[0]))
    {
        return encodeError("ERR only (P)SUBSCRIBE / (P)UNSUBSCRIBE / PING allowed in subscribed state");
    }

    auto it = table.find(argv[0]);
    if (it == table.end())
    {
        if (argv[0] == "OBJECT")
        {
            return wrongArity("OBJECT");
        }

        return encodeError("ERR unknown command '" + argv[0] + "'");
    }

    if (!arityOk(it->second, argv.size()))
    {
        return wrongArity(argv[0]);
    }

    return {};
}

string queueInMulti(CommandContext& ctx, const vector<string>& argv, const CommandTable& table)
{
    ctx.client.queued_commands.push_back(argv);

    string err = validateQueuedCommand(ctx.client, argv, table);
    if (!err.empty())
    {
        ctx.client.multi_error = true;
        return err;
    }

    return encodeSimpleString("QUEUED");
}

string execMulti(CommandContext& ctx,
    const function<string(CommandContext&, const vector<string>&)>& run)
{
    if (ctx.client.multi_error)
    {
        clearMultiState(ctx.client);
        return encodeError("EXECABORT Transaction discarded because of previous errors.");
    }

    if (ctx.client.dirty)
    {
        clearMultiState(ctx.client);
        clearClientWatches(ctx.client);
        return encodeNullArray();
    }

    vector<string> replies;
    replies.reserve(ctx.client.queued_commands.size());

    CommandContext exec_ctx = ctx;
    exec_ctx.exec_replay = true;

    for (const vector<string>& queued : ctx.client.queued_commands)
    {
        replies.push_back(run(exec_ctx, queued));
    }

    clearMultiState(ctx.client);
    clearClientWatches(ctx.client);
    return encodeRespArray(replies);
}

string discardMulti(Client& client)
{
    clearMultiState(client);
    return encodeOK();
}

string commandWatch(CommandContext& ctx, const vector<string>& argv)
{
    if (ctx.client.in_multi)
    {
        return encodeError("ERR WATCH inside MULTI is not allowed");
    }

    if (argv.size() < 2)
    {
        return wrongArity("WATCH");
    }

    for (size_t i = 1; i < argv.size(); ++i)
    {
        const string wkey = watchKey(ctx.client.db_index, argv[i]);
        ctx.client.watches.insert(wkey);
        watched_keys[wkey].insert(ctx.client.fd);
    }

    return encodeOK();
}
}

bool tryTransaction(CommandContext& ctx, const vector<string>& argv, string& reply)
{
    if (ctx.exec_replay)
    {
        return false;
    }

    const CommandTable& table = commandTable();
    const string& cmd = argv[0];

    if (cmd == "WATCH")
    {
        reply = commandWatch(ctx, argv);
        return true;
    }

    if (ctx.client.in_multi)
    {
        if (cmd == "EXEC")
        {
            reply = execMulti(ctx, [](CommandContext& inner, const vector<string>& queued) {
                return executeCommand(inner, queued);
            });
            return true;
        }

        if (cmd == "DISCARD")
        {
            reply = discardMulti(ctx.client);
            return true;
        }

        if (cmd == "MULTI")
        {
            reply = encodeError("ERR MULTI calls can not be nested");
            return true;
        }

        reply = queueInMulti(ctx, argv, table);
        return true;
    }

    if (cmd == "MULTI")
    {
        ctx.client.in_multi = true;
        ctx.client.multi_error = false;
        ctx.client.queued_commands.clear();
        reply = encodeOK();
        return true;
    }

    if (cmd == "EXEC")
    {
        reply = encodeError("ERR EXEC without MULTI");
        return true;
    }

    if (cmd == "DISCARD")
    {
        reply = encodeError("ERR DISCARD without MULTI");
        return true;
    }

    return false;
}

void watchCleanup(int fd, unordered_map<int, Client>& clients)
{
    auto it = clients.find(fd);
    if (it == clients.end())
    {
        return;
    }

    clearClientWatches(it->second);
}

void notifyWriteKeys(CommandContext& ctx, const vector<string>& argv, uint32_t flags)
{
    if ((flags & CMD_WRITE) == 0 || argv.empty())
    {
        return;
    }

    const string& cmd = argv[0];
    if (cmd == "FLUSHDB")
    {
        notifyDbFlushed(ctx, ctx.client.db_index, false);
        return;
    }

    if (cmd == "FLUSHALL")
    {
        notifyDbFlushed(ctx, ctx.client.db_index, true);
        return;
    }

    vector<string> keys;
    for (size_t pos : keyPositions(argv))
    {
        if (pos < argv.size())
        {
            keys.push_back(argv[pos]);
        }
    }

    notifyKeysModified(ctx, keys);
}
