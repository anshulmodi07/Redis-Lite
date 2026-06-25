#pragma once

#include "resp.h"

#include <string>
#include <unordered_set>
#include <vector>

struct Client
{
    int fd = -1;
    int db_index = 0;
    bool pubsub_mode = false;
    bool in_multi = false;
    bool multi_error = false;
    bool dirty = false;
    std::unordered_set<std::string> watches;
    std::vector<std::vector<std::string>> queued_commands;
    RespParser parser;
    std::vector<std::string> pending_writes;
    size_t write_chunk_idx = 0;
    size_t write_chunk_off = 0;
    bool closing = false;
};

bool clientHasPendingWrites(const Client& client);
void clientAppendWrite(Client& client, std::string data);
bool clientFlush(Client& client);
