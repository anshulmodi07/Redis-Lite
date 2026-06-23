#pragma once

#include "resp.h"

#include <string>

struct Client
{
    int fd = -1;
    RespParser parser;
    std::string write_buf;
    bool closing = false;
};
