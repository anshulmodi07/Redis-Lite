#include "parser.h"

#include "cmd_expire.h"
#include "cmd_hash.h"
#include "cmd_list.h"
#include "cmd_set.h"
#include "cmd_string.h"
#include "cmd_zset.h"
#include "encoding.h"
#include "object.h"
#include "resp.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

using namespace std;

namespace
{
using Db = unordered_map<string, RedisObject*>;

string uppercase(string value)
{
    transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(toupper(ch)); });
    return value;
}

string wrongArity(const string& command)
{
    return encodeError("ERR wrong number of arguments for '" + command + "' command");
}

string parseQuotedToken(const string& line, size_t& pos)
{
    string token;
    ++pos;

    while (pos < line.size())
    {
        char ch = line[pos++];

        if (ch == '\\')
        {
            if (pos >= line.size())
            {
                throw invalid_argument("unterminated escape sequence");
            }

            token.push_back(line[pos++]);
            continue;
        }

        if (ch == '"')
        {
            if (pos < line.size() && !isspace(static_cast<unsigned char>(line[pos])))
            {
                throw invalid_argument("quoted token must end before the next argument");
            }

            return token;
        }

        token.push_back(ch);
    }

    throw invalid_argument("unterminated quoted string");
}

string parseBareToken(const string& line, size_t& pos)
{
    size_t start = pos;

    while (pos < line.size() && !isspace(static_cast<unsigned char>(line[pos])))
    {
        ++pos;
    }

    return line.substr(start, pos - start);
}

string commandPing(const vector<string>& argv)
{
    if (argv.size() != 1)
    {
        return wrongArity(argv[0]);
    }

    return encodeSimpleString("PONG");
}

string commandType(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end())
    {
        return encodeSimpleString("none");
    }

    return encodeSimpleString(objectTypeName(it->second->type));
}

string commandDel(const vector<string>& argv, RedisDb& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    long long removed = 0;
    for (size_t i = 1; i < argv.size(); ++i)
    {
        auto it = db.data.find(argv[i]);
        if (it != db.data.end())
        {
            destroyObject(it->second);
            db.data.erase(it);
            db.expires.erase(argv[i]);
            ++removed;
        }
    }

    return encodeInteger(removed);
}

string commandExists(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    long long count = 0;
    for (size_t i = 1; i < argv.size(); ++i)
    {
        if (db.find(argv[i]) != db.end())
        {
            ++count;
        }
    }

    return encodeInteger(count);
}

string commandObjectEncoding(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 3 || uppercase(argv[1]) != "ENCODING")
    {
        return wrongArity("OBJECT");
    }

    auto it = db.find(argv[2]);
    if (it == db.end())
    {
        return encodeNullBulk();
    }

    return encodeBulkString(objectEncodingName(it->second));
}

vector<size_t> keyPositions(const vector<string>& argv)
{
    if (argv.empty())
    {
        return {};
    }

    const string& cmd = argv[0];
    vector<size_t> positions;
    auto key1 = [&]() {
        if (argv.size() > 1) positions.push_back(1);
    };

    if (cmd == "MGET" || cmd == "DEL" || cmd == "EXISTS" || cmd == "SINTER" || cmd == "SUNION" || cmd == "SDIFF")
    {
        for (size_t i = 1; i < argv.size(); ++i) positions.push_back(i);
    }
    else if (cmd == "MSET")
    {
        for (size_t i = 1; i < argv.size(); i += 2) positions.push_back(i);
    }
    else if (cmd == "SINTERSTORE" || cmd == "SUNIONSTORE" || cmd == "SDIFFSTORE")
    {
        for (size_t i = 1; i < argv.size(); ++i) positions.push_back(i);
    }
    else if (cmd == "EXPIRE" || cmd == "PEXPIRE" || cmd == "EXPIREAT" || cmd == "PEXPIREAT"
        || cmd == "TTL" || cmd == "PTTL" || cmd == "PERSIST")
    {
        key1();
    }
    else
    {
        key1();
    }

    return positions;
}
}

vector<string> tokenize(const string& line)
{
    vector<string> argv;
    size_t pos = 0;

    while (pos < line.size())
    {
        while (pos < line.size() && isspace(static_cast<unsigned char>(line[pos])))
        {
            ++pos;
        }

        if (pos >= line.size())
        {
            break;
        }

        if (line[pos] == '"')
        {
            argv.push_back(parseQuotedToken(line, pos));
        }
        else
        {
            argv.push_back(parseBareToken(line, pos));
        }
    }

    if (!argv.empty())
    {
        argv[0] = uppercase(argv[0]);
    }

    return argv;
}

string dispatchCommands(const vector<string>& argv, RedisDb& db)
{
    if (argv.empty())
    {
        return encodeError("ERR unknown command");
    }

    vector<string> normalized = argv;
    normalized[0] = uppercase(normalized[0]);

    if (normalized[0] == "PING")
    {
        return commandPing(normalized);
    }

    if (normalized[0] == "SET" || normalized[0] == "GET" || normalized[0] == "GETSET"
        || normalized[0] == "MSET" || normalized[0] == "MGET" || normalized[0] == "SETNX"
        || normalized[0] == "SETEX" || normalized[0] == "INCR" || normalized[0] == "DECR"
        || normalized[0] == "INCRBY" || normalized[0] == "DECRBY" || normalized[0] == "APPEND"
        || normalized[0] == "STRLEN")
    {
        return dispatchStringCommand(normalized, db);
    }

    if (normalized[0] == "EXPIRE" || normalized[0] == "PEXPIRE" || normalized[0] == "EXPIREAT"
        || normalized[0] == "PEXPIREAT" || normalized[0] == "TTL" || normalized[0] == "PTTL"
        || normalized[0] == "PERSIST")
    {
        return dispatchExpireCommand(normalized, db);
    }

    if (normalized[0] == "HSET" || normalized[0] == "HMSET" || normalized[0] == "HGET"
        || normalized[0] == "HMGET" || normalized[0] == "HDEL" || normalized[0] == "HEXISTS"
        || normalized[0] == "HLEN" || normalized[0] == "HKEYS" || normalized[0] == "HVALS"
        || normalized[0] == "HGETALL" || normalized[0] == "HINCRBY")
    {
        return dispatchHashCommand(normalized, db.data);
    }

    if (normalized[0] == "LPUSH" || normalized[0] == "RPUSH" || normalized[0] == "LPOP"
        || normalized[0] == "RPOP" || normalized[0] == "LLEN" || normalized[0] == "LRANGE"
        || normalized[0] == "LINDEX" || normalized[0] == "LSET" || normalized[0] == "LINSERT"
        || normalized[0] == "LREM" || normalized[0] == "LTRIM")
    {
        return dispatchListCommand(normalized, db.data);
    }

    if (normalized[0] == "SADD" || normalized[0] == "SREM" || normalized[0] == "SMEMBERS"
        || normalized[0] == "SCARD" || normalized[0] == "SISMEMBER" || normalized[0] == "SMISMEMBER"
        || normalized[0] == "SPOP" || normalized[0] == "SRANDMEMBER" || normalized[0] == "SINTER"
        || normalized[0] == "SUNION" || normalized[0] == "SDIFF" || normalized[0] == "SINTERSTORE"
        || normalized[0] == "SUNIONSTORE" || normalized[0] == "SDIFFSTORE")
    {
        return dispatchSetCommand(normalized, db.data);
    }

    if (normalized[0] == "ZADD" || normalized[0] == "ZRANGE" || normalized[0] == "ZRANGEBYSCORE"
        || normalized[0] == "ZREVRANGE" || normalized[0] == "ZRANK" || normalized[0] == "ZREVRANK"
        || normalized[0] == "ZSCORE" || normalized[0] == "ZCARD" || normalized[0] == "ZCOUNT"
        || normalized[0] == "ZREM" || normalized[0] == "ZINCRBY" || normalized[0] == "ZPOPMIN"
        || normalized[0] == "ZPOPMAX")
    {
        return dispatchZSetCommand(normalized, db.data);
    }

    if (normalized[0] == "TYPE")
    {
        return commandType(normalized, db.data);
    }

    if (normalized[0] == "DEL")
    {
        return commandDel(normalized, db);
    }

    if (normalized[0] == "EXISTS")
    {
        return commandExists(normalized, db.data);
    }

    if (normalized[0] == "OBJECT")
    {
        return commandObjectEncoding(normalized, db.data);
    }

    return encodeError("ERR unknown command");
}

string dispatch(const vector<string>& argv, RedisDb& db)
{
    vector<string> normalized = argv;
    if (!normalized.empty())
    {
        normalized[0] = uppercase(normalized[0]);
    }

    set<size_t> seen;
    for (size_t pos : keyPositions(normalized))
    {
        if (pos < normalized.size() && seen.insert(pos).second)
        {
            expireIfNeeded(db, normalized[pos]);
        }
    }

    return dispatchCommands(normalized, db);
}
