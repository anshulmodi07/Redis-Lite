#pragma once

#include "db.h"

#include <string>
#include <vector>

enum class AofFsyncPolicy
{
    Always,
    EverySec,
    No
};

extern std::string g_aof_filename;
extern bool g_aof_enabled;
extern bool g_aof_replaying;
extern AofFsyncPolicy g_aof_fsync_policy;

void aofInit();
bool aofSetEnabled(bool enable);
void aofAppendCommand(const std::vector<std::string>& argv);
void aofFlush();
void aofPeriodic(int elapsed_ms);
bool aofLoad(std::vector<RedisDb>& databases);

bool bgrewriteInProgress();
bool startBgrewrite(const std::vector<RedisDb>& databases);
void checkBgrewriteChild();

std::string aofFsyncPolicyName(AofFsyncPolicy policy);
bool parseAofFsyncPolicy(const std::string& value, AofFsyncPolicy& out);
