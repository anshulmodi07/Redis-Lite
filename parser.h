#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

std::vector<std::string> tokenize(const std::string& line);

std::string dispatch(
    const std::vector<std::string>& argv,
    std::unordered_map<std::string, std::string>& db,
    std::mutex& db_mutex);
