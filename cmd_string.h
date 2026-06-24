#pragma once

#include "db.h"

#include <string>
#include <vector>

std::string dispatchStringCommand(
    const std::vector<std::string>& argv,
    RedisDb& db);
