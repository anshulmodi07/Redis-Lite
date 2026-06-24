#pragma once

#include "client.h"
#include "db.h"

#include <string>
#include <vector>

std::vector<std::string> tokenize(const std::string& line);

std::string dispatch(
    Client& client,
    std::vector<RedisDb>& databases,
    const std::vector<std::string>& argv);
