#include "multi.h"

#include "pubsub.h"
#include "resp.h"

#include <functional>

using namespace std;

namespace
{
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

    vector<string> replies;
    replies.reserve(ctx.client.queued_commands.size());

    CommandContext exec_ctx = ctx;
    exec_ctx.exec_replay = true;

    for (const vector<string>& queued : ctx.client.queued_commands)
    {
        replies.push_back(run(exec_ctx, queued));
    }

    clearMultiState(ctx.client);
    return encodeRespArray(replies);
}

string discardMulti(Client& client)
{
    clearMultiState(client);
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
