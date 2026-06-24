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
    std::string write_buf;
    bool closing = false;
};
