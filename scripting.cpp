#include "scripting.h"

#include "commands.h"
#include "parser.h"
#include "resp.h"
#include "sha1.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <cctype>
#include <unordered_map>

using namespace std;

namespace
{
lua_State* lua_state = nullptr;
unordered_map<string, string> script_cache;
CommandContext* active_script_ctx = nullptr;
bool script_running = false;

string uppercase(string value)
{
    transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(toupper(ch));
    });
    return value;
}

int pushRespToLua(lua_State* L, const string& resp)
{
    if (resp.empty())
    {
        lua_pushnil(L);
        return 1;
    }

    switch (resp[0])
    {
    case '+':
        lua_pushlstring(L, resp.data() + 1, resp.size() - 3);
        return 1;
    case ':':
        lua_pushinteger(L, stoll(resp.substr(1, resp.size() - 3)));
        return 1;
    case '$':
    {
        const long long len = stoll(resp.substr(1, resp.find("\r\n") - 1));
        if (len < 0)
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        const size_t start = resp.find("\r\n") + 2;
        lua_pushlstring(L, resp.data() + start, static_cast<size_t>(len));
        return 1;
    }
    case '*':
    {
        const long long count = stoll(resp.substr(1, resp.find("\r\n") - 1));
        if (count < 0)
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        lua_newtable(L);
        size_t pos = resp.find("\r\n") + 2;
        for (long long i = 1; i <= count; ++i)
        {
            const size_t end = resp.find("\r\n", pos);
            const string item = resp.substr(pos, end - pos);
            pos = end + 2;
            if (item[0] == '$')
            {
                const long long len = stoll(item.substr(1));
                if (len < 0)
                {
                    lua_pushboolean(L, 0);
                }
                else
                {
                    lua_pushlstring(L, resp.data() + pos, static_cast<size_t>(len));
                    pos += static_cast<size_t>(len) + 2;
                }
            }
            else if (item[0] == ':')
            {
                lua_pushinteger(L, stoll(item.substr(1)));
            }
            else
            {
                lua_pushlstring(L, item.data() + 1, item.size() - 1);
            }

            lua_rawseti(L, -2, static_cast<int>(i));
        }

        return 1;
    }
    case '-':
        return luaL_error(L, "%s", resp.substr(1, resp.size() - 3).c_str());
    default:
        lua_pushnil(L);
        return 1;
    }
}

int redisCall(lua_State* L, bool raise_error)
{
    const int argc = lua_gettop(L);
    vector<string> argv;
    argv.reserve(static_cast<size_t>(argc));
    for (int i = 1; i <= argc; ++i)
    {
        size_t len = 0;
        const char* text = lua_tolstring(L, i, &len);
        argv.emplace_back(text, len);
    }

    if (argv.empty())
    {
        return luaL_error(L, "redis.call requires at least one argument");
    }

    argv[0] = uppercase(argv[0]);
    const string reply = executeCommand(*active_script_ctx, argv);
    if (!raise_error && reply.size() > 0 && reply[0] == '-')
    {
        lua_newtable(L);
        lua_pushlstring(L, reply.data() + 1, reply.size() - 3);
        lua_setfield(L, -2, "err");
        return 1;
    }

    return pushRespToLua(L, reply);
}

int lRedisCall(lua_State* L)
{
    return redisCall(L, true);
}

int lRedisPcall(lua_State* L)
{
    return redisCall(L, false);
}

void populateKeysArgv(lua_State* L, const vector<string>& keys, const vector<string>& args)
{
    lua_newtable(L);
    for (size_t i = 0; i < keys.size(); ++i)
    {
        lua_pushlstring(L, keys[i].data(), keys[i].size());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    lua_setglobal(L, "KEYS");

    lua_newtable(L);
    for (size_t i = 0; i < args.size(); ++i)
    {
        lua_pushlstring(L, args[i].data(), args[i].size());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    lua_setglobal(L, "ARGV");
}

string runScript(const string& source, const vector<string>& keys, const vector<string>& args, CommandContext& ctx)
{
    if (lua_state == nullptr)
    {
        return encodeError("ERR Lua library not available");
    }

    populateKeysArgv(lua_state, keys, args);
    active_script_ctx = &ctx;
    script_running = true;

    const int status = luaL_loadbuffer(lua_state, source.data(), source.size(), "user_script");
    if (status != 0)
    {
        string err = lua_tostring(lua_state, -1);
        lua_pop(lua_state, 1);
        active_script_ctx = nullptr;
        script_running = false;
        return encodeError("ERR Error compiling script (new function): " + err);
    }

    const int run_status = lua_pcall(lua_state, 0, 1, 0);
    if (run_status != 0)
    {
        string err = lua_tostring(lua_state, -1);
        lua_pop(lua_state, 1);
        active_script_ctx = nullptr;
        script_running = false;
        return encodeError("ERR Error running script (new function): " + err);
    }

    string reply;
    switch (lua_type(lua_state, -1))
    {
    case LUA_TNUMBER:
        reply = encodeInteger(static_cast<long long>(lua_tointeger(lua_state, -1)));
        break;
    case LUA_TSTRING:
    {
        size_t len = 0;
        const char* text = lua_tolstring(lua_state, -1, &len);
        reply = encodeBulkString(string(text, len));
        break;
    }
    case LUA_TBOOLEAN:
        reply = encodeInteger(lua_toboolean(lua_state, -1) ? 1 : 0);
        break;
    case LUA_TTABLE:
        reply = encodeArray({});
        break;
    default:
        reply = encodeNullBulk();
        break;
    }

    lua_pop(lua_state, 1);
    active_script_ctx = nullptr;
    script_running = false;
    return reply;
}

string commandEval(CommandContext& ctx, const vector<string>& argv, bool by_sha)
{
    if (argv.size() < 3)
    {
        return encodeError("ERR wrong number of arguments for '" + argv[0] + "' command");
    }

    long long numkeys = 0;
    try
    {
        numkeys = stoll(argv[2]);
    }
    catch (...)
    {
        return encodeError("ERR value is not an integer or out of range");
    }

    if (numkeys < 0 || static_cast<size_t>(3 + numkeys) > argv.size())
    {
        return encodeError("ERR Number of keys can't be negative");
    }

    string source;
    if (by_sha)
    {
        auto it = script_cache.find(argv[1]);
        if (it == script_cache.end())
        {
            return encodeError("NOSCRIPT No matching script. Please use EVAL.");
        }

        source = it->second;
    }
    else
    {
        source = argv[1];
    }

    vector<string> keys(argv.begin() + 3, argv.begin() + 3 + static_cast<size_t>(numkeys));
    vector<string> args(argv.begin() + 3 + static_cast<size_t>(numkeys), argv.end());
    return runScript(source, keys, args, ctx);
}

string commandScript(CommandContext&, const vector<string>& argv)
{
    if (argv.size() < 2)
    {
        return encodeError("ERR wrong number of arguments for 'SCRIPT' command");
    }

    string sub = uppercase(argv[1]);
    if (sub == "LOAD")
    {
        if (argv.size() != 3)
        {
            return encodeError("ERR wrong number of arguments for 'SCRIPT LOAD' command");
        }

        const string digest = sha1Hex(argv[2]);
        script_cache[digest] = argv[2];
        return encodeBulkString(digest);
    }

    if (sub == "EXISTS")
    {
        if (argv.size() < 3)
        {
            return encodeError("ERR wrong number of arguments for 'SCRIPT EXISTS' command");
        }

        string reply = "*" + to_string(argv.size() - 2) + "\r\n";
        for (size_t i = 2; i < argv.size(); ++i)
        {
            reply += encodeInteger(script_cache.count(argv[i]) > 0 ? 1 : 0);
        }

        return reply;
    }

    if (sub == "FLUSH")
    {
        script_cache.clear();
        return encodeOK();
    }

    return encodeError("ERR Unknown subcommand or wrong number of arguments for 'SCRIPT'");
}
}

void initScripting()
{
    if (lua_state != nullptr)
    {
        return;
    }

    lua_state = luaL_newstate();
    luaL_openlibs(lua_state);

    lua_newtable(lua_state);
    lua_pushcfunction(lua_state, lRedisCall);
    lua_setfield(lua_state, -2, "call");
    lua_pushcfunction(lua_state, lRedisPcall);
    lua_setfield(lua_state, -2, "pcall");
    lua_setglobal(lua_state, "redis");
}

void shutdownScripting()
{
    if (lua_state != nullptr)
    {
        lua_close(lua_state);
        lua_state = nullptr;
    }
}

bool scriptingRunning()
{
    return script_running;
}

void registerScriptingCommands(CommandTable& table)
{
    table["EVAL"] = Command{
        "EVAL",
        [](CommandContext& ctx, const vector<string>& argv) { return commandEval(ctx, argv, false); },
        -3,
        CMD_WRITE};
    table["EVALSHA"] = Command{
        "EVALSHA",
        [](CommandContext& ctx, const vector<string>& argv) { return commandEval(ctx, argv, true); },
        -3,
        CMD_WRITE};
    table["SCRIPT"] = Command{
        "SCRIPT",
        commandScript,
        -2,
        CMD_READONLY};
}
