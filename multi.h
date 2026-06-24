#pragma once

#include "commands.h"

#include <string>
#include <unordered_map>
#include <vector>

bool tryTransaction(CommandContext& ctx, const std::vector<std::string>& argv, std::string& reply);
void watchCleanup(int fd, std::unordered_map<int, Client>& clients);
void notifyWriteKeys(CommandContext& ctx, const std::vector<std::string>& argv, uint32_t flags);
