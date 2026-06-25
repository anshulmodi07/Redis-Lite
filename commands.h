#pragma once

#include "client.h"
#include "db.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

constexpr uint32_t CMD_READONLY = 1u;
constexpr uint32_t CMD_WRITE = 2u;

struct ServerStats
{
    long long start_time_ms = 0;
    long long total_commands_processed = 0;
    long long total_connections_received = 0;
    long long ops_per_sec = 0;

    // Internal trackers for computing ops_per_sec
    long long last_sample_time_ms = 0;
    long long last_sample_commands = 0;
};

extern ServerStats g_stats;

struct CommandContext
{
    Client& client;
    std::vector<RedisDb>& databases;
    std::unordered_map<int, Client>* clients = nullptr;
    int epoll_fd = -1;
    bool exec_replay = false;

    RedisDb& db()
    {
        return databases.at(static_cast<size_t>(client.db_index));
    }

    const RedisDb& db() const
    {
        return databases.at(static_cast<size_t>(client.db_index));
    }
};

using CommandFunc = std::function<std::string(CommandContext&, const std::vector<std::string>&)>;

struct Command
{
    std::string name;
    CommandFunc func;
    int arity = 0;
    uint32_t flags = 0;
};

using CommandTable = std::unordered_map<std::string, Command>;

void initCommandTable();
const CommandTable& commandTable();
std::string executeCommand(CommandContext& ctx, const std::vector<std::string>& argv);

void registerStringCommands(CommandTable& table);
void registerExpireCommands(CommandTable& table);
void registerHashCommands(CommandTable& table);
void registerListCommands(CommandTable& table);
void registerSetCommands(CommandTable& table);
void registerZSetCommands(CommandTable& table);
