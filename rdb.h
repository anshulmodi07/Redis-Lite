#pragma once

#include "db.h"

#include <string>
#include <vector>

extern std::string g_rdb_filename;

bool saveRDB(const std::string& path, const std::vector<RedisDb>& databases);
bool loadRDB(const std::string& path, std::vector<RedisDb>& databases);
