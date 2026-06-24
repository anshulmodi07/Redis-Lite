#include "parser.h"

#include "commands.h"
#include "eviction.h"
#include "resp.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

using namespace std;

namespace
{
string uppercase(string value)
{
    transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(toupper(ch)); });
    return value;
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
        if (argv.size() > 1)
        {
            positions.push_back(1);
        }
    };

    if (cmd == "MGET" || cmd == "DEL" || cmd == "EXISTS" || cmd == "SINTER" || cmd == "SUNION" || cmd == "SDIFF")
    {
        for (size_t i = 1; i < argv.size(); ++i)
        {
            positions.push_back(i);
        }
    }
    else if (cmd == "MSET")
    {
        for (size_t i = 1; i < argv.size(); i += 2)
        {
            positions.push_back(i);
        }
    }
    else if (cmd == "SINTERSTORE" || cmd == "SUNIONSTORE" || cmd == "SDIFFSTORE")
    {
        for (size_t i = 1; i < argv.size(); ++i)
        {
            positions.push_back(i);
        }
    }
    else if (cmd == "RENAME" || cmd == "RENAMENX")
    {
        if (argv.size() > 1)
        {
            positions.push_back(1);
        }

        if (argv.size() > 2)
        {
            positions.push_back(2);
        }
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

string dispatch(Client& client, vector<RedisDb>& databases, const vector<string>& argv,
    unordered_map<int, Client>* clients, int epoll_fd)
{
    vector<string> normalized = argv;
    if (!normalized.empty())
    {
        normalized[0] = uppercase(normalized[0]);
    }

    CommandContext ctx{client, databases, clients, epoll_fd};
    set<size_t> seen;
    for (size_t pos : keyPositions(normalized))
    {
        if (pos < normalized.size() && seen.insert(pos).second)
        {
            expireIfNeeded(ctx.db(), normalized[pos]);
            auto it = ctx.db().data.find(normalized[pos]);
            if (it != ctx.db().data.end())
            {
                touchObject(it->second);
            }
        }
    }

    return executeCommand(ctx, normalized);
}
