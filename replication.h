#pragma once

#include "client.h"
#include "commands.h"
#include "db.h"

#include <string>
#include <unordered_map>
#include <vector>

void initReplication();
void replicationSetContext(std::vector<RedisDb>* databases, std::unordered_map<int, Client>* clients, int epoll_fd);
void replicationStartReplica(int epoll_fd);
void replicationPeriodic(int epoll_fd);
void replicationCleanupClient(int fd);
bool replicationHandleCommand(Client& client, const std::vector<std::string>& argv, std::string& reply);
bool replicationPreflightWrite(const Client& client, std::string& err);
void replicationFeedWrite(const std::vector<std::string>& argv);
void replicationHandleMasterEvent(int epoll_fd);
std::string replicationInfoSection();
bool replicationIsMasterLink(int fd);
