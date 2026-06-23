#pragma once

#include "object.h"

#include <string>
#include <vector>

std::string dispatchStringCommand(
    const std::vector<std::string>& argv,
    Db& db);
