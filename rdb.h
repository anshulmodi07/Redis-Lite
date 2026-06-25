#pragma once

#include "db.h"

#include <string>
#include <vector>

extern std::string g_rdb_filename;

bool saveRDB(const std::string& path, const std::vector<RedisDb>& databases);
std::string serializeRDB(const std::vector<RedisDb>& databases);
bool loadRDB(const std::string& path, std::vector<RedisDb>& databases);
bool loadRDBFromBuffer(const std::string& data, std::vector<RedisDb>& databases);

bool bgsaveInProgress();
bool startBgsave(const std::vector<RedisDb>& databases);
void checkBgsaveChild();
