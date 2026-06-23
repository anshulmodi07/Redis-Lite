#include "parser.h"

#include "object.h"
#include "resp.h"

#include <algorithm>
#include <cctype>
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

void storeString(Db& db, const string& key, const string& value)
{
    auto it = db.find(key);
    if (it != db.end())
    {
        destroyObject(it->second);
        db.erase(it);
    }

    db[key] = createStringObject(value);
}

string commandPing(const vector<string>& argv)
{
    if (argv.size() != 1)
    {
        return wrongArity(argv[0]);
    }

    return encodeSimpleString("PONG");
}

string commandSet(const vector<string>& argv, Db& db)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }

    storeString(db, argv[1], argv[2]);
    return encodeOK();
}

string commandGet(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    auto it = db.find(argv[1]);
    if (it == db.end() || it->second->type != OBJ_STRING)
    {
        if (it != db.end())
        {
            return encodeError(
                "WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        return encodeNullBulk();
    }

    return encodeBulkString(getStringValue(it->second));
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

string commandDel(const vector<string>& argv, Db& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    long long removed = 0;
    for (size_t i = 1; i < argv.size(); ++i)
    {
        auto it = db.find(argv[i]);
        if (it != db.end())
        {
            destroyObject(it->second);
            db.erase(it);
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

string dispatch(const vector<string>& argv, Db& db)
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

    if (normalized[0] == "SET")
    {
        return commandSet(normalized, db);
    }

    if (normalized[0] == "GET")
    {
        return commandGet(normalized, db);
    }

    if (normalized[0] == "TYPE")
    {
        return commandType(normalized, db);
    }

    if (normalized[0] == "DEL")
    {
        return commandDel(normalized, db);
    }

    if (normalized[0] == "EXISTS")
    {
        return commandExists(normalized, db);
    }

    return encodeError("ERR unknown command");
}
