#include "pubsub.h"

#include "eventloop.h"
#include "resp.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace std;

unordered_map<string, unordered_set<int>> channel_to_clients;
unordered_map<int, unordered_set<string>> client_channels;
unordered_map<int, unordered_set<string>> client_patterns;

namespace
{
string wrongArity(const string& command)
{
    return encodeError("ERR wrong number of arguments for '" + command + "' command");
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

void removeChannel(int fd, const string& channel)
{
    channel_to_clients[channel].erase(fd);
    if (channel_to_clients[channel].empty())
    {
        channel_to_clients.erase(channel);
    }

    client_channels[fd].erase(channel);
}

void pushToClient(CommandContext& ctx, int fd, const string& frame)
{
    if (ctx.clients == nullptr)
    {
        return;
    }

    auto it = ctx.clients->find(fd);
    if (it == ctx.clients->end())
    {
        return;
    }

    clientAppendWrite(it->second, frame);
    if (ctx.epoll_fd >= 0)
    {
        clientWritePending(ctx.epoll_fd, it->second);
    }
}

long long publishMessage(CommandContext& ctx, const string& channel, const string& message)
{
    if (ctx.clients == nullptr)
    {
        return 0;
    }

    string frame = encodeArray({"message", channel, message});
    unordered_set<int> delivered;

    auto ch_it = channel_to_clients.find(channel);
    if (ch_it != channel_to_clients.end())
    {
        for (int fd : ch_it->second)
        {
            pushToClient(ctx, fd, frame);
            delivered.insert(fd);
        }
    }

    for (const auto& item : client_patterns)
    {
        int fd = item.first;
        if (delivered.count(fd) != 0)
        {
            continue;
        }

        for (const string& pattern : item.second)
        {
            if (globMatch(pattern, channel))
            {
                string pframe = encodeArray({"pmessage", pattern, channel, message});
                pushToClient(ctx, fd, pframe);
                delivered.insert(fd);
                break;
            }
        }
    }

    return static_cast<long long>(delivered.size());
}

string commandSubscribe(CommandContext& ctx, const vector<string>& argv)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    string reply;
    for (size_t i = 1; i < argv.size(); ++i)
    {
        const string& channel = argv[i];
        channel_to_clients[channel].insert(ctx.client.fd);
        client_channels[ctx.client.fd].insert(channel);
        ctx.client.pubsub_mode = true;
        reply += encodeArray({"subscribe", channel, to_string(pubsubSubscriptionCount(ctx.client.fd))});
    }

    return reply;
}

string commandUnsubscribe(CommandContext& ctx, const vector<string>& argv)
{
    string reply;
    if (argv.size() == 1)
    {
        vector<string> channels(client_channels[ctx.client.fd].begin(), client_channels[ctx.client.fd].end());
        for (const string& channel : channels)
        {
            removeChannel(ctx.client.fd, channel);
            reply += encodeArray({"unsubscribe", channel, to_string(pubsubSubscriptionCount(ctx.client.fd))});
        }
    }
    else
    {
        for (size_t i = 1; i < argv.size(); ++i)
        {
            removeChannel(ctx.client.fd, argv[i]);
            reply += encodeArray({"unsubscribe", argv[i], to_string(pubsubSubscriptionCount(ctx.client.fd))});
        }
    }

    if (pubsubSubscriptionCount(ctx.client.fd) == 0)
    {
        ctx.client.pubsub_mode = false;
    }

    return reply;
}

string commandPsubscribe(CommandContext& ctx, const vector<string>& argv)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    string reply;
    for (size_t i = 1; i < argv.size(); ++i)
    {
        client_patterns[ctx.client.fd].insert(argv[i]);
        ctx.client.pubsub_mode = true;
        reply += encodeArray({"psubscribe", argv[i], to_string(pubsubSubscriptionCount(ctx.client.fd))});
    }

    return reply;
}

string commandPunsubscribe(CommandContext& ctx, const vector<string>& argv)
{
    string reply;
    if (argv.size() == 1)
    {
        vector<string> patterns(client_patterns[ctx.client.fd].begin(), client_patterns[ctx.client.fd].end());
        for (const string& pattern : patterns)
        {
            client_patterns[ctx.client.fd].erase(pattern);
            reply += encodeArray({"punsubscribe", pattern, to_string(pubsubSubscriptionCount(ctx.client.fd))});
        }
    }
    else
    {
        for (size_t i = 1; i < argv.size(); ++i)
        {
            client_patterns[ctx.client.fd].erase(argv[i]);
            reply += encodeArray({"punsubscribe", argv[i], to_string(pubsubSubscriptionCount(ctx.client.fd))});
        }
    }

    if (client_patterns[ctx.client.fd].empty())
    {
        client_patterns.erase(ctx.client.fd);
    }

    if (pubsubSubscriptionCount(ctx.client.fd) == 0)
    {
        ctx.client.pubsub_mode = false;
    }

    return reply;
}

string commandPublish(CommandContext& ctx, const vector<string>& argv)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }

    return encodeInteger(publishMessage(ctx, argv[1], argv[2]));
}

string commandPubsub(CommandContext&, const vector<string>& argv)
{
    if (argv.size() < 2)
    {
        return wrongArity("PUBSUB");
    }

    string sub = argv[1];
    transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char ch) {
        return static_cast<char>(toupper(ch));
    });

    if (sub == "CHANNELS")
    {
        string pattern = argv.size() >= 3 ? argv[2] : "*";
        vector<string> channels;
        for (const auto& item : channel_to_clients)
        {
            if (!item.second.empty() && globMatch(pattern, item.first))
            {
                channels.push_back(item.first);
            }
        }

        sort(channels.begin(), channels.end());
        return encodeArray(channels);
    }

    if (sub == "NUMSUB")
    {
        vector<string> out;
        if (argv.size() == 2)
        {
            for (const auto& item : channel_to_clients)
            {
                out.push_back(item.first);
                out.push_back(to_string(item.second.size()));
            }
        }
        else
        {
            for (size_t i = 2; i < argv.size(); ++i)
            {
                auto it = channel_to_clients.find(argv[i]);
                long long count = it == channel_to_clients.end() ? 0 : static_cast<long long>(it->second.size());
                out.push_back(argv[i]);
                out.push_back(to_string(count));
            }
        }

        return encodeArray(out);
    }

    return wrongArity("PUBSUB");
}
}

void pubsubCleanup(int fd)
{
    for (const string& channel : client_channels[fd])
    {
        channel_to_clients[channel].erase(fd);
        if (channel_to_clients[channel].empty())
        {
            channel_to_clients.erase(channel);
        }
    }

    client_channels.erase(fd);
    client_patterns.erase(fd);
}

bool clientInPubsubMode(int fd)
{
    return client_channels.count(fd) != 0 || client_patterns.count(fd) != 0;
}

long long pubsubSubscriptionCount(int fd)
{
    long long count = 0;
    auto ch = client_channels.find(fd);
    if (ch != client_channels.end())
    {
        count += static_cast<long long>(ch->second.size());
    }

    auto pat = client_patterns.find(fd);
    if (pat != client_patterns.end())
    {
        count += static_cast<long long>(pat->second.size());
    }

    return count;
}

void registerPubsubCommands(CommandTable& table)
{
    auto add = [&](const char* name, int arity, auto fn) {
        table[name] = Command{name, fn, arity, CMD_READONLY};
    };

    add("SUBSCRIBE", -2, [](CommandContext& ctx, const vector<string>& argv) {
        return commandSubscribe(ctx, argv);
    });
    add("UNSUBSCRIBE", -1, [](CommandContext& ctx, const vector<string>& argv) {
        return commandUnsubscribe(ctx, argv);
    });
    add("PSUBSCRIBE", -2, [](CommandContext& ctx, const vector<string>& argv) {
        return commandPsubscribe(ctx, argv);
    });
    add("PUNSUBSCRIBE", -1, [](CommandContext& ctx, const vector<string>& argv) {
        return commandPunsubscribe(ctx, argv);
    });
    add("PUBLISH", 3, [](CommandContext& ctx, const vector<string>& argv) {
        return commandPublish(ctx, argv);
    });
    add("PUBSUB", -2, [](CommandContext& ctx, const vector<string>& argv) {
        return commandPubsub(ctx, argv);
    });
}

bool pubsubAllowsInMode(const string& cmd)
{
    return cmd == "SUBSCRIBE" || cmd == "UNSUBSCRIBE" || cmd == "PSUBSCRIBE" || cmd == "PUNSUBSCRIBE"
        || cmd == "PING";
}
