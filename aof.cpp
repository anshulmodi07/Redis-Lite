#include "aof.h"

#include "client.h"
#include "commands.h"
#include "db.h"
#include "encoding.h"
#include "object.h"
#include "parser.h"
#include "resp.h"
#include "rdb.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

string g_aof_filename = "appendonly.aof";
bool g_aof_enabled = false;
bool g_aof_replaying = false;
AofFsyncPolicy g_aof_fsync_policy = AofFsyncPolicy::EverySec;

namespace
{
int aof_fd = -1;
string aof_buf;
int aof_timer_ms = 0;

#if defined(__linux__) || defined(__APPLE__)
pid_t bgrewrite_child_pid = 0;
bool bgrewrite_in_progress = false;
#endif

void closeAofFd()
{
    if (aof_fd >= 0)
    {
        close(aof_fd);
        aof_fd = -1;
    }
}

void reopenAofFd()
{
    closeAofFd();
    aof_fd = open(g_aof_filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    g_aof_enabled = aof_fd >= 0;
}

void aofFsync()
{
    if (aof_fd >= 0)
    {
        fdatasync(aof_fd);
    }
}

void writeEncoded(ofstream& out, const vector<string>& argv)
{
    string frame = encodeArray(argv);
    out.write(frame.data(), static_cast<streamsize>(frame.size()));
}

bool writeCompactAof(const string& path, const vector<RedisDb>& databases)
{
    ofstream out(path, ios::binary | ios::trunc);
    if (!out)
    {
        return false;
    }

    for (size_t db_index = 0; db_index < databases.size(); ++db_index)
    {
        const RedisDb& db = databases[db_index];
        if (db.data.empty())
        {
            continue;
        }

        if (db_index != 0)
        {
            writeEncoded(out, {"SELECT", to_string(db_index)});
        }

        for (const auto& item : db.data)
        {
            const string& key = item.first;
            const RedisObject* obj = item.second;
            long long pttl = ttlMilliseconds(db, key);

            switch (obj->type)
            {
            case OBJ_STRING:
            {
                vector<string> cmd = {"SET", key, getStringValue(obj)};
                if (pttl > 0)
                {
                    cmd.push_back("PX");
                    cmd.push_back(to_string(pttl));
                }

                writeEncoded(out, cmd);
                break;
            }
            case OBJ_HASH:
            {
                vector<string> cmd = {"HSET", key};
                for (const string& field : hashGetAllFlat(obj))
                {
                    cmd.push_back(field);
                }

                if (cmd.size() > 2)
                {
                    writeEncoded(out, cmd);
                }

                break;
            }
            case OBJ_LIST:
            {
                vector<string> items = listRange(obj, 0, -1);
                if (!items.empty())
                {
                    vector<string> cmd = {"RPUSH", key};
                    cmd.insert(cmd.end(), items.begin(), items.end());
                    writeEncoded(out, cmd);
                }

                break;
            }
            case OBJ_SET:
            {
                vector<string> members = setMembers(obj);
                if (!members.empty())
                {
                    vector<string> cmd = {"SADD", key};
                    cmd.insert(cmd.end(), members.begin(), members.end());
                    writeEncoded(out, cmd);
                }

                break;
            }
            case OBJ_ZSET:
            {
                vector<ZSetEntry> entries = zsetRangeByRank(obj, 0, -1, false);
                if (!entries.empty())
                {
                    vector<string> cmd = {"ZADD", key};
                    for (const ZSetEntry& entry : entries)
                    {
                        cmd.push_back(to_string(entry.score));
                        cmd.push_back(entry.member);
                    }

                    writeEncoded(out, cmd);
                }

                break;
            }
            default:
                break;
            }
        }
    }

    return static_cast<bool>(out);
}
}

void aofInit()
{
    reopenAofFd();
}

void aofAppendCommand(const vector<string>& argv)
{
    if (!g_aof_enabled || g_aof_replaying || aof_fd < 0 || argv.empty())
    {
        return;
    }

    aof_buf += encodeArray(argv);
    if (g_aof_fsync_policy == AofFsyncPolicy::Always)
    {
        aofFlush();
        aofFsync();
    }
}

void aofFlush()
{
    if (aof_fd < 0 || aof_buf.empty())
    {
        return;
    }

    const char* data = aof_buf.data();
    size_t left = aof_buf.size();
    while (left > 0)
    {
        ssize_t written = write(aof_fd, data, left);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            cerr << "AOF write failed: " << strerror(errno) << "\n";
            break;
        }

        data += written;
        left -= static_cast<size_t>(written);
    }

    aof_buf.clear();
}

void aofPeriodic(int elapsed_ms)
{
    aofFlush();
    if (g_aof_fsync_policy != AofFsyncPolicy::EverySec)
    {
        return;
    }

    aof_timer_ms += elapsed_ms;
    if (aof_timer_ms >= 1000)
    {
        aofFsync();
        aof_timer_ms = 0;
    }
}

bool aofLoad(vector<RedisDb>& databases)
{
    ifstream in(g_aof_filename, ios::binary);
    if (!in)
    {
        return false;
    }

    string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    if (content.empty())
    {
        return true;
    }

    g_aof_replaying = true;
    Client client;
    RespParser parser;
    parser.feed(content.data(), content.size());

    vector<string> argv;
    while (parser.tryParse(argv))
    {
        CommandContext ctx{client, databases};
        executeCommand(ctx, argv);
    }

    g_aof_replaying = false;
    return true;
}

string aofFsyncPolicyName(AofFsyncPolicy policy)
{
    switch (policy)
    {
    case AofFsyncPolicy::Always:
        return "always";
    case AofFsyncPolicy::EverySec:
        return "everysec";
    case AofFsyncPolicy::No:
        return "no";
    default:
        return "everysec";
    }
}

bool parseAofFsyncPolicy(const string& value, AofFsyncPolicy& out)
{
    string normalized;
    normalized.reserve(value.size());
    for (char ch : value)
    {
        normalized.push_back(static_cast<char>(tolower(static_cast<unsigned char>(ch))));
    }

    if (normalized == "always")
    {
        out = AofFsyncPolicy::Always;
    }
    else if (normalized == "everysec")
    {
        out = AofFsyncPolicy::EverySec;
    }
    else if (normalized == "no")
    {
        out = AofFsyncPolicy::No;
    }
    else
    {
        return false;
    }

    return true;
}

bool bgrewriteInProgress()
{
#if defined(__linux__) || defined(__APPLE__)
    return bgrewrite_in_progress;
#else
    return false;
#endif
}

bool startBgrewrite(const vector<RedisDb>& databases)
{
#if defined(__linux__) || defined(__APPLE__)
    if (bgrewrite_in_progress || bgsaveInProgress())
    {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        return false;
    }

    if (pid == 0)
    {
        string temp = g_aof_filename + ".rewrite";
        bool ok = writeCompactAof(temp, databases);
        if (ok)
        {
            ok = rename(temp.c_str(), g_aof_filename.c_str()) == 0;
        }

        _exit(ok ? 0 : 1);
    }

    bgrewrite_child_pid = pid;
    bgrewrite_in_progress = true;
    return true;
#else
    (void)databases;
    return false;
#endif
}

void checkBgrewriteChild()
{
#if defined(__linux__) || defined(__APPLE__)
    if (!bgrewrite_in_progress)
    {
        return;
    }

    int status = 0;
    pid_t pid = waitpid(bgrewrite_child_pid, &status, WNOHANG);
    if (pid == 0)
    {
        return;
    }

    bgrewrite_in_progress = false;
    bgrewrite_child_pid = 0;
    if (pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        reopenAofFd();
    }
#endif
}
