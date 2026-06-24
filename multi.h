#pragma once

#include "commands.h"

#include <string>
#include <vector>

bool tryTransaction(CommandContext& ctx, const std::vector<std::string>& argv, std::string& reply);
