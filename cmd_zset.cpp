#include "cmd_zset.h"

#include "commands.h"

#include "encoding.h"
#include "resp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace std;

namespace
{
string wrongArity(const string& command)
{
    return encodeError("ERR wrong number of arguments for '" + command + "' command");
}

string wrongType()
{
    return encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
}

string scoreString(double value)
{
    ostringstream out;
    out << setprecision(15) << value;
    return out.str();
}

string upper(string value)
{
    transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(toupper(ch));
    });
    return value;
}

bool parseDouble(const string& value, double& out)
{
    string normalized = upper(value);
    if (normalized == "-INF")
    {
        out = -numeric_limits<double>::infinity();
        return true;
    }
    if (normalized == "+INF" || normalized == "INF")
    {
        out = numeric_limits<double>::infinity();
        return true;
    }

    try
    {
        size_t used = 0;
        out = stod(value, &used);
        return used == value.size() && !isnan(out);
    }
    catch (const exception&)
    {
        return false;
    }
}

bool parseLong(const string& value, long long& out)
{
    try
    {
        size_t used = 0;
        out = stoll(value, &used);
        return used == value.size();
    }
    catch (const exception&)
    {
        return false;
    }
}

RedisObject* lookupZSet(Db& db, const string& key, bool create, bool& type_error)
{
    type_error = false;
    auto it = db.find(key);
    if (it == db.end())
    {
        if (!create)
        {
            return nullptr;
        }
        RedisObject* obj = createZSetObject();
        db[key] = obj;
        return obj;
    }
    if (it->second->type != OBJ_ZSET)
    {
        type_error = true;
        return nullptr;
    }
    return it->second;
}

const RedisObject* getZSet(const Db& db, const string& key, bool& type_error)
{
    type_error = false;
    auto it = db.find(key);
    if (it == db.end())
    {
        return nullptr;
    }
    if (it->second->type != OBJ_ZSET)
    {
        type_error = true;
        return nullptr;
    }
    return it->second;
}

string encodeEntries(const vector<ZSetEntry>& entries, bool with_scores)
{
    vector<string> out;
    out.reserve(with_scores ? entries.size() * 2 : entries.size());
    for (const auto& entry : entries)
    {
        out.push_back(entry.member);
        if (with_scores)
        {
            out.push_back(scoreString(entry.score));
        }
    }
    return encodeArray(out);
}

string commandZAdd(const vector<string>& argv, Db& db)
{
    if (argv.size() < 4)
    {
        return wrongArity(argv[0]);
    }

    bool nx = false, xx = false, gt = false, lt = false, ch = false;
    size_t pos = 2;
    while (pos < argv.size())
    {
        string opt = upper(argv[pos]);
        if (opt == "NX") nx = true;
        else if (opt == "XX") xx = true;
        else if (opt == "GT") gt = true;
        else if (opt == "LT") lt = true;
        else if (opt == "CH") ch = true;
        else break;
        ++pos;
    }
    if ((nx && xx) || ((gt || lt) && nx) || (gt && lt) || pos >= argv.size() || ((argv.size() - pos) % 2) != 0)
    {
        return encodeError("ERR syntax error");
    }

    bool type_error = false;
    RedisObject* zset = lookupZSet(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    vector<pair<double, string>> entries;
    for (; pos < argv.size(); pos += 2)
    {
        double score = 0;
        if (!parseDouble(argv[pos], score) || isinf(score))
        {
            return encodeError("ERR value is not a valid float");
        }

        entries.emplace_back(score, argv[pos + 1]);
    }

    long long added = 0;
    long long changed = 0;
    const long long result = zsetAdd(zset, entries, nx, xx, gt, lt, ch, added, changed);
    return encodeInteger(result);
}

string commandZRange(const vector<string>& argv, const Db& db, bool force_reverse)
{
    if (argv.size() < 4)
    {
        return wrongArity(argv[0]);
    }

    bool byscore = argv[0] == "ZRANGEBYSCORE", reverse = force_reverse, with_scores = false;
    long long offset = 0, count = -1;
    for (size_t i = 4; i < argv.size(); ++i)
    {
        string opt = upper(argv[i]);
        if (opt == "BYSCORE") byscore = true;
        else if (opt == "REV") reverse = true;
        else if (opt == "WITHSCORES") with_scores = true;
        else if (opt == "LIMIT" && i + 2 < argv.size())
        {
            if (!parseLong(argv[i + 1], offset) || !parseLong(argv[i + 2], count))
            {
                return encodeError("ERR value is out of range, or is not an integer");
            }
            i += 2;
        }
        else return encodeError("ERR syntax error");
    }

    bool type_error = false;
    const RedisObject* zset = getZSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }
    if (zset == nullptr)
    {
        return encodeArray({});
    }

    if (byscore)
    {
        double min = 0, max = 0;
        if (!parseDouble(argv[2], min) || !parseDouble(argv[3], max))
        {
            return encodeError("ERR min or max is not a float");
        }
        if (reverse)
        {
            swap(min, max);
        }
        return encodeEntries(zsetRangeByScore(zset, min, max, reverse, offset, count), with_scores);
    }

    long long start = 0, stop = 0;
    if (!parseLong(argv[2], start) || !parseLong(argv[3], stop))
    {
        return encodeError("ERR value is out of range, or is not an integer");
    }
    return encodeEntries(zsetRangeByRank(zset, start, stop, reverse), with_scores);
}

string commandZRank(const vector<string>& argv, const Db& db, bool reverse)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }
    bool type_error = false;
    const RedisObject* zset = getZSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }
    if (zset == nullptr)
    {
        return encodeNullBulk();
    }

    bool found = false;
    const long long rank = zsetRank(zset, argv[2], reverse, found);
    return found ? encodeInteger(rank) : encodeNullBulk();
}

string commandZScore(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }
    bool type_error = false;
    const RedisObject* zset = getZSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }
    double score = 0;
    if (zset == nullptr || !zsetScore(zset, argv[2], score))
    {
        return encodeNullBulk();
    }
    return encodeBulkString(scoreString(score));
}

string commandZCard(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }
    bool type_error = false;
    const RedisObject* zset = getZSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }
    return encodeInteger(zset == nullptr ? 0 : zsetCard(zset));
}

string commandZCount(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }
    double min = 0, max = 0;
    if (!parseDouble(argv[2], min) || !parseDouble(argv[3], max))
    {
        return encodeError("ERR min or max is not a float");
    }
    bool type_error = false;
    const RedisObject* zset = getZSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }
    return encodeInteger(zset == nullptr ? 0 : zsetCount(zset, min, max));
}

string commandZRem(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }
    bool type_error = false;
    RedisObject* zset = lookupZSet(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }
    if (zset == nullptr)
    {
        return encodeInteger(0);
    }

    return encodeInteger(zsetRem(zset, vector<string>(argv.begin() + 2, argv.end())));
}

string commandZIncrBy(const vector<string>& argv, Db& db)
{
    if (argv.size() != 4)
    {
        return wrongArity(argv[0]);
    }
    double inc = 0;
    if (!parseDouble(argv[2], inc) || isinf(inc))
    {
        return encodeError("ERR value is not a valid float");
    }
    bool type_error = false;
    RedisObject* zset = lookupZSet(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    const double next = zsetIncrBy(zset, argv[3], inc);
    if (isnan(next) || isinf(next))
    {
        return encodeError("ERR increment would produce NaN or Infinity");
    }

    return encodeBulkString(scoreString(next));
}

string commandZPop(const vector<string>& argv, Db& db, bool max_side)
{
    if (argv.size() < 2 || argv.size() > 3)
    {
        return wrongArity(argv[0]);
    }
    long long count = 1;
    if (argv.size() == 3 && (!parseLong(argv[2], count) || count < 0))
    {
        return encodeError("ERR value is out of range, or is not an integer");
    }
    bool type_error = false;
    RedisObject* zset = lookupZSet(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }
    if (zset == nullptr || count == 0)
    {
        return encodeArray({});
    }

    return encodeEntries(zsetPop(zset, count, max_side), true);
}
}

void registerZSetCommands(CommandTable& table)
{
    auto run = [&](const char* name, int arity, uint32_t flags, auto handler) {
        table[name] = Command{name, handler, arity, flags};
    };

    run("ZADD", -4, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZAdd(argv, ctx.db().data);
    });
    run("ZRANGE", -4, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZRange(argv, ctx.db().data, false);
    });
    run("ZRANGEBYSCORE", -4, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZRange(argv, ctx.db().data, false);
    });
    run("ZREVRANGE", -4, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZRange(argv, ctx.db().data, true);
    });
    run("ZRANK", 3, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZRank(argv, ctx.db().data, false);
    });
    run("ZREVRANK", 3, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZRank(argv, ctx.db().data, true);
    });
    run("ZSCORE", 3, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZScore(argv, ctx.db().data);
    });
    run("ZCARD", 2, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZCard(argv, ctx.db().data);
    });
    run("ZCOUNT", 4, CMD_READONLY, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZCount(argv, ctx.db().data);
    });
    run("ZREM", -3, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZRem(argv, ctx.db().data);
    });
    run("ZINCRBY", 4, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZIncrBy(argv, ctx.db().data);
    });
    run("ZPOPMIN", -2, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZPop(argv, ctx.db().data, false);
    });
    run("ZPOPMAX", -2, CMD_WRITE, [](CommandContext& ctx, const vector<string>& argv) {
        return commandZPop(argv, ctx.db().data, true);
    });
}
