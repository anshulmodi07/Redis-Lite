#pragma once

#include "db.h"

#include <string>
#include <vector>

std::string dispatchExpireCommand(
    const std::vector<std::string>& argv,
    RedisDb& db);
