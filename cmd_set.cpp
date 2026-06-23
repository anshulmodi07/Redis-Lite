#include "cmd_set.h"

#include "resp.h"

#include <cstdlib>
#include <unordered_set>
#include <vector>

using namespace std;

namespace
{
using RedisSet = unordered_set<string>;

string wrongArity(const string& command)
{
    return encodeError("ERR wrong number of arguments for '" + command + "' command");
}

string wrongType()
{
    return encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
}

RedisSet* lookupSet(Db& db, const string& key, bool create, bool& type_error)
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
        return static_cast<RedisSet*>(obj->ptr);
    }

    if (it->second->type != OBJ_SET)
    {
        type_error = true;
        return nullptr;
    }

    return static_cast<RedisSet*>(it->second->ptr);
}

const RedisSet* getSet(const Db& db, const string& key, bool& type_error)
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

    return static_cast<const RedisSet*>(it->second->ptr);
}

void storeSetMembers(Db& db, const string& key, const RedisSet& members)
{
    auto it = db.find(key);
    if (it != db.end())
    {
        destroyObject(it->second);
        db.erase(it);
    }

    RedisObject* obj = createSetObject();
    RedisSet* set = static_cast<RedisSet*>(obj->ptr);
    *set = members;
    db[key] = obj;
}

vector<string> setToVector(const RedisSet& set)
{
    return vector<string>(set.begin(), set.end());
}

string randomMember(const RedisSet& set)
{
    auto it = set.begin();
    advance(it, static_cast<long>(rand() % set.size()));
    return *it;
}

vector<string> randomMembers(const RedisSet& set, long long count)
{
    vector<string> members = setToVector(set);
    vector<string> picked;

    if (count > 0)
    {
        long long to_pick = min<long long>(count, static_cast<long long>(members.size()));
        for (long long i = 0; i < to_pick; ++i)
        {
            size_t idx = static_cast<size_t>(rand() % members.size());
            picked.push_back(members[idx]);
            members.erase(members.begin() + static_cast<long>(idx));
        }
    }
    else
    {
        long long picks = -count;
        for (long long i = 0; i < picks; ++i)
        {
            picked.push_back(randomMember(set));
        }
    }

    return picked;
}

bool collectSets(const Db& db, const vector<string>& keys, vector<const RedisSet*>& out, string& err)
{
    out.clear();
    out.reserve(keys.size());

    for (const string& key : keys)
    {
        bool type_error = false;
        const RedisSet* set = getSet(db, key, type_error);
        if (type_error)
        {
            err = wrongType();
            return false;
        }

        out.push_back(set);
    }

    return true;
}

RedisSet setIntersection(const vector<const RedisSet*>& sets)
{
    RedisSet result;

    for (const RedisSet* set : sets)
    {
        if (set == nullptr || set->empty())
        {
            return {};
        }
    }

    result = *sets.front();
    for (size_t i = 1; i < sets.size(); ++i)
    {
        RedisSet next;
        for (const string& member : result)
        {
            if (sets[i]->count(member) > 0)
            {
                next.insert(member);
            }
        }

        result = std::move(next);
    }

    return result;
}

RedisSet setUnion(const vector<const RedisSet*>& sets)
{
    RedisSet result;
    for (const RedisSet* set : sets)
    {
        if (set != nullptr)
        {
            result.insert(set->begin(), set->end());
        }
    }

    return result;
}

RedisSet setDifference(const vector<const RedisSet*>& sets)
{
    RedisSet result;
    if (sets.empty())
    {
        return result;
    }

    if (sets.front() != nullptr)
    {
        result = *sets.front();
    }

    for (size_t i = 1; i < sets.size(); ++i)
    {
        if (sets[i] == nullptr)
        {
            continue;
        }

        for (const string& member : *sets[i])
        {
            result.erase(member);
        }
    }

    return result;
}

string commandSAdd(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisSet* set = lookupSet(db, argv[1], true, type_error);
    if (type_error)
    {
        return wrongType();
    }

    long long added = 0;
    for (size_t i = 2; i < argv.size(); ++i)
    {
        if (set->insert(argv[i]).second)
        {
            ++added;
        }
    }

    return encodeInteger(added);
}

string commandSRem(const vector<string>& argv, Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    RedisSet* set = lookupSet(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr)
    {
        return encodeInteger(0);
    }

    long long removed = 0;
    for (size_t i = 2; i < argv.size(); ++i)
    {
        removed += static_cast<long long>(set->erase(argv[i]));
    }

    return encodeInteger(removed);
}

string commandSMembers(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    const RedisSet* set = getSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr)
    {
        return encodeArray({});
    }

    return encodeArray(setToVector(*set));
}

string commandSCard(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 2)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    const RedisSet* set = getSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr)
    {
        return encodeInteger(0);
    }

    return encodeInteger(static_cast<long long>(set->size()));
}

string commandSIsMember(const vector<string>& argv, const Db& db)
{
    if (argv.size() != 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    const RedisSet* set = getSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr)
    {
        return encodeInteger(0);
    }

    return encodeInteger(set->count(argv[2]) > 0 ? 1 : 0);
}

string commandSMIsMember(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    bool type_error = false;
    const RedisSet* set = getSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    string reply = "*" + to_string(argv.size() - 2) + "\r\n";
    for (size_t i = 2; i < argv.size(); ++i)
    {
        long long present = 0;
        if (set != nullptr && set->count(argv[i]) > 0)
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
    RedisSet* set = lookupSet(db, argv[1], false, type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr || set->empty())
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

    vector<string> popped = randomMembers(*set, count);
    for (const string& member : popped)
    {
        set->erase(member);
    }

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
    const RedisSet* set = getSet(db, argv[1], type_error);
    if (type_error)
    {
        return wrongType();
    }

    if (set == nullptr || set->empty())
    {
        return encodeNullBulk();
    }

    if (argv.size() == 2)
    {
        return encodeBulkString(randomMember(*set));
    }

    long long count = stoll(argv[2]);
    return encodeArray(randomMembers(*set, count));
}

string commandSInter(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    vector<const RedisSet*> sets;
    string err;
    vector<string> keys(argv.begin() + 1, argv.end());
    if (!collectSets(db, keys, sets, err))
    {
        return err;
    }

    return encodeArray(setToVector(setIntersection(sets)));
}

string commandSUnion(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    vector<const RedisSet*> sets;
    string err;
    vector<string> keys(argv.begin() + 1, argv.end());
    if (!collectSets(db, keys, sets, err))
    {
        return err;
    }

    return encodeArray(setToVector(setUnion(sets)));
}

string commandSDiff(const vector<string>& argv, const Db& db)
{
    if (argv.size() < 2)
    {
        return wrongArity(argv[0]);
    }

    vector<const RedisSet*> sets;
    string err;
    vector<string> keys(argv.begin() + 1, argv.end());
    if (!collectSets(db, keys, sets, err))
    {
        return err;
    }

    return encodeArray(setToVector(setDifference(sets)));
}

string commandStoreOp(const vector<string>& argv, Db& db, RedisSet (*op)(const vector<const RedisSet*>&))
{
    if (argv.size() < 3)
    {
        return wrongArity(argv[0]);
    }

    vector<const RedisSet*> sets;
    string err;
    vector<string> keys(argv.begin() + 2, argv.end());
    if (!collectSets(db, keys, sets, err))
    {
        return err;
    }

    RedisSet result = op(sets);
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
