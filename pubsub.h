#pragma once

#include "client.h"
#include "commands.h"

#include <string>
#include <unordered_map>
#include <vector>

void registerPubsubCommands(CommandTable& table);
void pubsubCleanup(int fd);
bool clientInPubsubMode(int fd);
bool pubsubAllowsInMode(const std::string& cmd);
long long pubsubSubscriptionCount(int fd);
