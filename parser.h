#pragma once

#include "client.h"
#include "db.h"

#include <string>
#include <unordered_map>
#include <vector>

void tokenize(const std::string &line, std::vector<std::string> &out);
std::vector<size_t> keyPositions(const std::vector<std::string> &argv);

std::string dispatch(Client &client, std::vector<RedisDb> &databases,
                     std::vector<std::string> &argv,
                     std::unordered_map<int, Client> *clients = nullptr,
                     int epoll_fd = -1);
