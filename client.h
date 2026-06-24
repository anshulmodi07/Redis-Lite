#pragma once

#include "resp.h"

#include <string>
#include <vector>

struct Client
{
    int fd = -1;
    int db_index = 0;
    bool pubsub_mode = false;
    bool in_multi = false;
    bool multi_error = false;
    std::vector<std::vector<std::string>> queued_commands;
    RespParser parser;
    std::string write_buf;
    bool closing = false;
};
