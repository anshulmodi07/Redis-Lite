#pragma once

#include "object.h"

#include <string>
#include <vector>

std::string dispatchHashCommand(
    const std::vector<std::string>& argv,
    Db& db);
