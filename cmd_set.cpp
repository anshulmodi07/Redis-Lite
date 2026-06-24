#include "cmd_set.h"

#include "encoding.h"
#include "resp.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>
#include <vector>

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

RedisObject* lookupSet(Db& db, const string& key, bool create, bool& type_error)
{
    type_error = false;
    auto it = db.find(key);

    if (it == db.end())
    {
        if (!create)
        {
            return nullptr;
        }

        RedisObject* obj = createSetObject();
        db[key] = obj;
        return obj;
    }

    if (it->second->type != OBJ_SET)
    {
        type_error = true;
        return nullptr;
    }

    return it->second;
}

const RedisObject* getSetObject(const Db& db, const string& key, bool& type_error)
{
    type_error = false;
    auto it = db.find(key);

    if (it == db.end())
    {
        return nullptr;
    }

    if (it->second->type != OBJ_SET)
    {
        type_error = true;
        return nullptr;
    }

    return it->second;
}

void storeSetMembers(Db& db, const string& key, const vector<string>& members)
{
    auto it = db.find(key);
    if (it != db.end())
    {
        destroyObject(it->second);
        db.erase(it);
    }

    RedisObject* obj = createSetObject();
    setReplaceMembers(obj, members);
    db[key] = obj;
}

string randomMember(const vector<string>& members)
{
    return members[static_cast<size_t>(rand() % members.size())];
}

vector<string> randomMembers(const vector<string>& members, long long count)
{
    vector<string> pool = members;
    vector<string> picked;

    if (count > 0)
    {
        const long long to_pick = min<long long>(count, static_cast<long long>(pool.size()));
        for (long long i = 0; i < to_pick; ++i)
        {
            const size_t idx = static_cast<size_t>(rand() % pool.size());
            picked.push_back(pool[idx]);
            pool.erase(pool.begin() + static_cast<long>(idx));
        }
    }
    else
    {
        const long long picks = -count;
        for (long long i = 0; i < picks; ++i)
        {
            picked.push_back(randomMember(members));
        }
    }

    return picked;
}

bool collectSetMembers(const Db& db, const vector<string>& keys, vector<vector<string>>& out, string& err)
{
    out.clear();
    out.reserve(keys.size());

    for (const string& key : keys)
    {
        bool type_error = false;
        const RedisObject* set = getSetObject(db, key, type_error);
        if (type_error)
        {
            err = wrongType();
            return false;
        }

        out.push_back(set == nullptr ? vector<string>{} : setMembers(set));
    }

    return true;
}

vector<string> setIntersection(const vector<vector<string>>& sets)
{
    if (sets.empty())
    {
        return {};
    }

    for (const vector<string>& set : sets)
    {
        if (set.empty())
        {
            return {};
        }
    }

    vector<string> result = sets.front();
    for (size_t i = 1; i < sets.size(); ++i)
    {
        vector<string> next;
        for (const string& member : result)
        {
            if (find(sets[i].begin(), sets[i].end(), member) != sets[i].end())
            {
                next.push_back(member);
            }
        }

        result = std::move(next);
    }

    return result;
}

vector<string> setUnion(const vector<vector<string>>& sets)
{
    unordered_set<string> result;
    for (const vector<string>& set : sets)
    {
        result.insert(set.begin(), set.end());
    }

    return vector<string>(result.begin(), result.end());
}

vector<string> setDifference(const vector<vector<string>>& sets)
{
    if (sets.empty())
    {
        return {};
    }

    unordered_set<string> result(sets.front().begin(), sets.front().end());
    for (size_t i = 1; i < sets.size(); ++i)
    {
        for (const string& member : sets[i])
        {
            result.erase(member);
        }
    }

    return vector<string>(result.begin(), result.end());
}

string commandSAdd(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* set = lookupSet(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    return encodeInteger(setAdd(set, vector<string>(argv.begin() + 2, argv.end())));
}

string commandSRem(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* set = lookupSet(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr)
    {
        return encodeInteger(0);
    }

    return encodeInteger(setRem(set, vector<string>(argv.begin() + 2, argv.end())));
}

string commandSMembers(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    const RedisObject* set = getSetObject(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr)
    {
        return encodeArray({});
    }

    return encodeArray(setMembers(set));
}

string commandSCard(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    const RedisObject* set = getSetObject(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr)
    {
        return encodeInteger(0);
    }

    return encodeInteger(setCard(set));
}

string commandSIsMember(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    const RedisObject* set = getSetObject(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr)
    {
        return encodeInteger(0);
    }

    return encodeInteger(setIsMember(set, argv[2]) ? 1 : 0);
}

string commandSMIsMember(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    const RedisObject* set = getSetObject(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    string reply = "*" + to_string(argv.size() - 2) + "\r\n";
    for (size_t i = 2; i < argv.size(); ++i)
    {
        long long present = 0;
        if (set != nullptr && setIsMember(set, argv[i]))
        {
            present = 1;
        }

        reply += encodeInteger(present);
    }

    return reply;
}

string commandSPop(const vector<string>& argv, Db& db)
{
    if (argv.size() < 2 || argv.size() > 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisObject* set = lookupSet(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr || setCard(set) == 0)
    {
        if (argv.size() == 3)
        {
            return encodeArray({});
        }

        return encodeNullBulk();
    }

    long long count = 1;
    if (argv.size() == 3)
    {
        count = stoll(argv[2]);
        if (count < 0)
        {
            return encodeError("ERR value is out of range, or is not an integer");
        }
    }

    const vector<string> members = setMembers(set);
    const vector<string> popped = randomMembers(members, count);
    setRem(set, popped);

    if (argv.size() == 2)
    {
        return encodeBulkString(popped.front());
    }

    return encodeArray(popped);
}

string commandSRandMember(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 2 || argv.size() > 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    const RedisObject* set = getSetObject(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr || setCard(set) == 0)
    {
        return encodeNullBulk();
    }

    const vector<string> members = setMembers(set);
    if (argv.size() == 2)
    {
        return encodeBulkString(randomMember(members));
    }

    return encodeArray(randomMembers(members, stoll(argv[2])));
}

string commandSInter(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    vector<vector<string>> sets;
    string err;
    if (!collectSetMembers(db, vector<string>(argv.begin() + 1, argv.end()), sets, err))
    {
        return err;
    }

    return encodeArray(setIntersection(sets));
}

string commandSUnion(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    vector<vector<string>> sets;
    string err;
    if (!collectSetMembers(db, vector<string>(argv.begin() + 1, argv.end()), sets, err))
    {
        return err;
    }

    return encodeArray(setUnion(sets));
}

string commandSDiff(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    vector<vector<string>> sets;
    string err;
    if (!collectSetMembers(db, vector<string>(argv.begin() + 1, argv.end()), sets, err))
    {
        return err;
    }

    return encodeArray(setDifference(sets));
}

string commandStoreOp(const vector<string>& argv, Db& db, vector<string> (*op)(const vector<vector<string>>&))
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    vector<vector<string>> sets;
    string err;
    if (!collectSetMembers(db, vector<string>(argv.begin() + 2, argv.end()), sets, err))
    {
        return err;
    }

    const vector<string> result = op(sets);
    storeSetMembers(db, argv[1], result);
    return encodeInteger(static_cast<long long>(result.size()));
}
}

string dispatchSetCommand(const vector<string>& argv, Db& db)
{
    const string& cmd = argv[0];

    if (cmd == "SADD")
    {
        return commandSAdd(argv, db);
    }

    if (cmd == "SREM")
    {
        return commandSRem(argv, db);
    }

    if (cmd == "SMEMBERS")
    {
        return commandSMembers(argv, db);
    }

    if (cmd == "SCARD")
    {
        return commandSCard(argv, db);
    }

    if (cmd == "SISMEMBER")
    {
        return commandSIsMember(argv, db);
    }

    if (cmd == "SMISMEMBER")
    {
        return commandSMIsMember(argv, db);
    }

    if (cmd == "SPOP")
    {
        return commandSPop(argv, db);
    }

    if (cmd == "SRANDMEMBER")
    {
        return commandSRandMember(argv, db);
    }

    if (cmd == "SINTER")
    {
        return commandSInter(argv, db);
    }

    if (cmd == "SUNION")
    {
        return commandSUnion(argv, db);
    }

    if (cmd == "SDIFF")
    {
        return commandSDiff(argv, db);
    }

    if (cmd == "SINTERSTORE")
    {
        return commandStoreOp(argv, db, setIntersection);
    }

    if (cmd == "SUNIONSTORE")
    {
        return commandStoreOp(argv, db, setUnion);
    }

    if (cmd == "SDIFFSTORE")
    {
        return commandStoreOp(argv, db, setDifference);
    }

    return encodeError("ERR unknown command");
}
