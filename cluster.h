#pragma once

#include "commands.h"

#include <string>
#include <vector>

void initCluster();
void registerClusterCommands(CommandTable& table);
std::string clusterPreflight(const std::vector<std::string>& argv);
std::string clusterInfoSection();
void clusterPeriodic();
bool clusterHandleBus(int fd);

uint16_t clusterKeySlot(const std::string& key);
bool clusterSlotIsLocal(uint16_t slot);
std::string clusterSlotEndpoint(uint16_t slot);
