#include "encoding.h"

#include "intset.h"
#include "listpack.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <unordered_map>
#include <unordered_set>

using namespace std;

namespace
{
using HashMap = unordered_map<string, string>;
using RedisList = list<string>;
using RedisSet = unordered_set<string>;

string lpEntryToString(listpackEntry entry)
{
    long long val = 0;
    unsigned char* sval = nullptr;
    uint32_t slen = 0;
    const int kind = lpGet(entry, &val, &sval, &slen);
    if (kind == 1)
    {
        return to_string(val);
    }
    if (kind == 0)
    {
        return string(reinterpret_cast<char*>(sval), slen);
    }

    return {};
}

vector<string> listpackToStrings(listpack lp)
{
    vector<string> out;
    if (lp == nullptr)
    {
        return out;
    }

    out.reserve(lpLength(lp));
    for (listpackEntry entry = lpFirst(lp); entry != nullptr; entry = lpNext(lp, entry))
    {
        out.push_back(lpEntryToString(entry));
    }

    return out;
}

listpack stringsToListpack(const vector<string>& items)
{
    listpack lp = lpNew();
    for (const string& item : items)
    {
        lp = lpAppend(
            lp,
            reinterpret_cast<const unsigned char*>(item.data()),
            static_cast<uint32_t>(item.size()));
        if (lp == nullptr)
        {
            break;
        }
    }

    return lp;
}

vector<pair<string, string>> listpackToHashPairs(listpack lp)
{
    vector<pair<string, string>> pairs;
    if (lp == nullptr)
    {
        return pairs;
    }

    listpackEntry entry = lpFirst(lp);
    while (entry != nullptr)
    {
        const string field = lpEntryToString(entry);
        entry = lpNext(lp, entry);
        if (entry == nullptr)
        {
            break;
        }

        pairs.emplace_back(field, lpEntryToString(entry));
        entry = lpNext(lp, entry);
    }

    return pairs;
}

listpack hashPairsToListpack(const vector<pair<string, string>>& pairs)
{
    listpack lp = lpNew();
    for (const auto& pair : pairs)
    {
        lp = lpAppend(
            lp,
            reinterpret_cast<const unsigned char*>(pair.first.data()),
            static_cast<uint32_t>(pair.first.size()));
        lp = lpAppend(
            lp,
            reinterpret_cast<const unsigned char*>(pair.second.data()),
            static_cast<uint32_t>(pair.second.size()));
    }

    return lp;
}

vector<pair<double, string>> listpackToZSetPairs(listpack lp)
{
    vector<pair<double, string>> pairs;
    if (lp == nullptr)
    {
        return pairs;
    }

    listpackEntry entry = lpFirst(lp);
    while (entry != nullptr)
    {
        const string score_text = lpEntryToString(entry);
        entry = lpNext(lp, entry);
        if (entry == nullptr)
        {
            break;
        }

        pairs.emplace_back(stod(score_text), lpEntryToString(entry));
        entry = lpNext(lp, entry);
    }

    return pairs;
}

listpack zsetPairsToListpack(const vector<pair<double, string>>& pairs)
{
    listpack lp = lpNew();
    for (const auto& pair : pairs)
    {
        const string score_text = to_string(pair.first);
        lp = lpAppend(
            lp,
            reinterpret_cast<const unsigned char*>(score_text.data()),
            static_cast<uint32_t>(score_text.size()));
        lp = lpAppend(
            lp,
            reinterpret_cast<const unsigned char*>(pair.second.data()),
            static_cast<uint32_t>(pair.second.size()));
    }

    return lp;
}

vector<string> intsetToStrings(intset is)
{
    vector<string> out;
    out.reserve(intsetLen(is));
    for (uint32_t i = 0; i < intsetLen(is); ++i)
    {
        out.push_back(to_string(intsetGet(is, i)));
    }

    return out;
}

bool valueTooLargeForListpack(const string& value)
{
    return value.size() > LP_MAX_VALUE_SIZE;
}

bool hashPairsNeedPromotion(const vector<pair<string, string>>& pairs)
{
    if (pairs.size() > LP_MAX_ENTRIES)
    {
        return true;
    }

    for (const auto& pair : pairs)
    {
        if (valueTooLargeForListpack(pair.first) || valueTooLargeForListpack(pair.second))
        {
            return true;
        }
    }

    return false;
}

bool stringsNeedPromotion(const vector<string>& items)
{
    if (items.size() > LP_MAX_ENTRIES)
    {
        return true;
    }

    for (const string& item : items)
    {
        if (valueTooLargeForListpack(item))
        {
            return true;
        }
    }

    return false;
}

bool zsetPairsNeedPromotion(const vector<pair<double, string>>& pairs)
{
    if (pairs.size() > LP_MAX_ENTRIES)
    {
        return true;
    }

    for (const auto& pair : pairs)
    {
        if (valueTooLargeForListpack(pair.second))
        {
            return true;
        }
    }

    return false;
}

void freeObjectPayload(RedisObject* obj)
{
    switch (obj->type)
    {
    case OBJ_LIST:
        if (obj->encoding == ENC_LISTPACK)
        {
            lpFree(static_cast<listpack>(obj->ptr));
        }
        else
        {
            delete static_cast<RedisList*>(obj->ptr);
        }
        break;
    case OBJ_HASH:
        if (obj->encoding == ENC_LISTPACK)
        {
            lpFree(static_cast<listpack>(obj->ptr));
        }
        else
        {
            delete static_cast<HashMap*>(obj->ptr);
        }
        break;
    case OBJ_SET:
        if (obj->encoding == ENC_INTSET)
        {
            intsetFree(static_cast<intset>(obj->ptr));
        }
        else if (obj->encoding == ENC_LISTPACK)
        {
            lpFree(static_cast<listpack>(obj->ptr));
        }
        else
        {
            delete static_cast<RedisSet*>(obj->ptr);
        }
        break;
    case OBJ_ZSET:
        if (obj->encoding == ENC_LISTPACK)
        {
            lpFree(static_cast<listpack>(obj->ptr));
        }
        else
        {
            delete static_cast<ZSet*>(obj->ptr);
        }
        break;
    default:
        break;
    }
}

void promoteHashToHashtable(RedisObject* obj, const vector<pair<string, string>>& pairs)
{
    freeObjectPayload(obj);
    HashMap* map = new HashMap();
    for (const auto& pair : pairs)
    {
        (*map)[pair.first] = pair.second;
    }

    obj->encoding = ENC_HASHTABLE;
    obj->ptr = map;
}

void promoteListToQuicklist(RedisObject* obj, const vector<string>& items)
{
    freeObjectPayload(obj);
    RedisList* list = new RedisList(items.begin(), items.end());
    obj->encoding = ENC_QUICKLIST;
    obj->ptr = list;
}

void promoteSetToHashtable(RedisObject* obj, const vector<string>& members)
{
    freeObjectPayload(obj);
    RedisSet* set = new RedisSet(members.begin(), members.end());
    obj->encoding = ENC_HASHTABLE;
    obj->ptr = set;
}

void promoteSetToListpack(RedisObject* obj, const vector<string>& members)
{
    freeObjectPayload(obj);
    obj->encoding = ENC_LISTPACK;
    obj->ptr = stringsToListpack(members);
}

void promoteZSetToSkipList(RedisObject* obj, const vector<pair<double, string>>& pairs)
{
    freeObjectPayload(obj);
    ZSet* zset = new ZSet();
    for (const auto& pair : pairs)
    {
        zset->scores[pair.second] = pair.first;
        zset->index.insert(pair.first, pair.second);
    }

    obj->encoding = ENC_SKIPLIST;
    obj->ptr = zset;
}

void hashMaybePromote(RedisObject* obj, const vector<pair<string, string>>& pairs)
{
    if (obj->encoding != ENC_LISTPACK || !hashPairsNeedPromotion(pairs))
    {
        return;
    }

    promoteHashToHashtable(obj, pairs);
}

void listMaybePromote(RedisObject* obj, const vector<string>& items)
{
    if (obj->encoding != ENC_LISTPACK || !stringsNeedPromotion(items))
    {
        return;
    }

    promoteListToQuicklist(obj, items);
}

void setMaybePromote(RedisObject* obj, const vector<string>& members)
{
    if (obj->encoding == ENC_INTSET)
    {
        if (members.size() > INTSET_MAX_ENTRIES)
        {
            promoteSetToHashtable(obj, members);
        }
        return;
    }

    if (obj->encoding == ENC_LISTPACK && stringsNeedPromotion(members))
    {
        promoteSetToHashtable(obj, members);
    }
}

void zsetMaybePromote(RedisObject* obj, const vector<pair<double, string>>& pairs)
{
    if (obj->encoding != ENC_LISTPACK || !zsetPairsNeedPromotion(pairs))
    {
        return;
    }

    promoteZSetToSkipList(obj, pairs);
}

HashMap* hashAsMap(RedisObject* obj)
{
    return static_cast<HashMap*>(obj->ptr);
}

RedisList* listAsList(RedisObject* obj)
{
    return static_cast<RedisList*>(obj->ptr);
}

RedisSet* setAsSet(RedisObject* obj)
{
    return static_cast<RedisSet*>(obj->ptr);
}

ZSet* zsetAsZSet(RedisObject* obj)
{
    return static_cast<ZSet*>(obj->ptr);
}

long long normalizeIndex(long long index, long long size)
{
    if (index < 0)
    {
        index = size + index;
    }

    return index;
}
}

string objectEncodingName(const RedisObject* obj)
{
    if (obj == nullptr)
    {
        return "unknown";
    }

    switch (obj->encoding)
    {
    case ENC_RAW:
        return "raw";
    case ENC_INT:
        return "int";
    case ENC_LISTPACK:
        return "listpack";
    case ENC_QUICKLIST:
        return "quicklist";
    case ENC_HASHTABLE:
        return "hashtable";
    case ENC_SKIPLIST:
        return "skiplist";
    case ENC_INTSET:
        return "intset";
    }

    return "unknown";
}

long long hashLen(const RedisObject* obj)
{
    if (obj->encoding == ENC_LISTPACK)
    {
        return static_cast<long long>(listpackToHashPairs(static_cast<listpack>(obj->ptr)).size());
    }

    return static_cast<long long>(hashAsMap(const_cast<RedisObject*>(obj))->size());
}

bool hashGet(const RedisObject* obj, const string& field, string& out)
{
    if (obj->encoding == ENC_LISTPACK)
    {
        for (const auto& pair : listpackToHashPairs(static_cast<listpack>(obj->ptr)))
        {
            if (pair.first == field)
            {
                out = pair.second;
                return true;
            }
        }

        return false;
    }

    const HashMap* map = hashAsMap(const_cast<RedisObject*>(obj));
    auto it = map->find(field);
    if (it == map->end())
    {
        return false;
    }

    out = it->second;
    return true;
}

bool hashSet(RedisObject* obj, const string& field, const string& value, bool& added)
{
    if (obj->encoding == ENC_HASHTABLE)
    {
        HashMap* map = hashAsMap(obj);
        added = map->find(field) == map->end();
        (*map)[field] = value;
        return true;
    }

    vector<pair<string, string>> pairs = listpackToHashPairs(static_cast<listpack>(obj->ptr));
    added = true;
    bool found = false;
    for (auto& pair : pairs)
    {
        if (pair.first == field)
        {
            pair.second = value;
            added = false;
            found = true;
            break;
        }
    }

    if (!found)
    {
        pairs.emplace_back(field, value);
    }

    hashMaybePromote(obj, pairs);
    if (obj->encoding == ENC_HASHTABLE)
    {
        return true;
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = hashPairsToListpack(pairs);
    return true;
}

long long hashDel(RedisObject* obj, const vector<string>& fields)
{
    if (obj->encoding == ENC_HASHTABLE)
    {
        HashMap* map = hashAsMap(obj);
        long long removed = 0;
        for (const string& field : fields)
        {
            removed += static_cast<long long>(map->erase(field));
        }

        return removed;
    }

    vector<pair<string, string>> pairs = listpackToHashPairs(static_cast<listpack>(obj->ptr));
    long long removed = 0;
    for (const string& field : fields)
    {
        const size_t before = pairs.size();
        pairs.erase(
            remove_if(pairs.begin(), pairs.end(), [&](const pair<string, string>& entry) {
                return entry.first == field;
            }),
            pairs.end());
        if (pairs.size() != before)
        {
            ++removed;
        }
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = hashPairsToListpack(pairs);
    return removed;
}

bool hashExists(const RedisObject* obj, const string& field)
{
    string ignored;
    return hashGet(obj, field, ignored);
}

vector<string> hashKeys(const RedisObject* obj)
{
    vector<string> keys;
    if (obj->encoding == ENC_LISTPACK)
    {
        for (const auto& pair : listpackToHashPairs(static_cast<listpack>(obj->ptr)))
        {
            keys.push_back(pair.first);
        }

        return keys;
    }

    const HashMap* map = hashAsMap(const_cast<RedisObject*>(obj));
    keys.reserve(map->size());
    for (const auto& entry : *map)
    {
        keys.push_back(entry.first);
    }

    return keys;
}

vector<string> hashVals(const RedisObject* obj)
{
    vector<string> values;
    if (obj->encoding == ENC_LISTPACK)
    {
        for (const auto& pair : listpackToHashPairs(static_cast<listpack>(obj->ptr)))
        {
            values.push_back(pair.second);
        }

        return values;
    }

    const HashMap* map = hashAsMap(const_cast<RedisObject*>(obj));
    values.reserve(map->size());
    for (const auto& entry : *map)
    {
        values.push_back(entry.second);
    }

    return values;
}

vector<string> hashGetAllFlat(const RedisObject* obj)
{
    vector<string> flat;
    if (obj->encoding == ENC_LISTPACK)
    {
        for (const auto& pair : listpackToHashPairs(static_cast<listpack>(obj->ptr)))
        {
            flat.push_back(pair.first);
            flat.push_back(pair.second);
        }

        return flat;
    }

    const HashMap* map = hashAsMap(const_cast<RedisObject*>(obj));
    flat.reserve(map->size() * 2);
    for (const auto& entry : *map)
    {
        flat.push_back(entry.first);
        flat.push_back(entry.second);
    }

    return flat;
}

bool hashIncrBy(RedisObject* obj, const string& field, long long delta, long long& out)
{
    string current;
    long long value = 0;
    if (hashGet(obj, field, current))
    {
        if (!tryParseInteger(current, value))
        {
            return false;
        }
    }

    out = value + delta;
    bool added = false;
    hashSet(obj, field, to_string(out), added);
    return true;
}

long long listLen(const RedisObject* obj)
{
    if (obj->encoding == ENC_LISTPACK)
    {
        return static_cast<long long>(lpLength(static_cast<listpack>(obj->ptr)));
    }

    return static_cast<long long>(listAsList(const_cast<RedisObject*>(obj))->size());
}

void listPushFront(RedisObject* obj, const vector<string>& values)
{
    if (obj->encoding == ENC_QUICKLIST)
    {
        RedisList* list = listAsList(obj);
        for (auto it = values.rbegin(); it != values.rend(); ++it)
        {
            list->push_front(*it);
        }

        return;
    }

    vector<string> items = listpackToStrings(static_cast<listpack>(obj->ptr));
    items.insert(items.begin(), values.begin(), values.end());
    listMaybePromote(obj, items);
    if (obj->encoding == ENC_QUICKLIST)
    {
        return;
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = stringsToListpack(items);
}

void listPushBack(RedisObject* obj, const vector<string>& values)
{
    if (obj->encoding == ENC_QUICKLIST)
    {
        RedisList* list = listAsList(obj);
        list->insert(list->end(), values.begin(), values.end());
        return;
    }

    vector<string> items = listpackToStrings(static_cast<listpack>(obj->ptr));
    items.insert(items.end(), values.begin(), values.end());
    listMaybePromote(obj, items);
    if (obj->encoding == ENC_QUICKLIST)
    {
        return;
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = stringsToListpack(items);
}

vector<string> listPop(RedisObject* obj, bool from_head, long long count)
{
    vector<string> popped;
    if (count <= 0)
    {
        return popped;
    }

    if (obj->encoding == ENC_QUICKLIST)
    {
        RedisList* list = listAsList(obj);
        while (count > 0 && !list->empty())
        {
            if (from_head)
            {
                popped.push_back(list->front());
                list->pop_front();
            }
            else
            {
                popped.push_back(list->back());
                list->pop_back();
            }

            --count;
        }

        return popped;
    }

    vector<string> items = listpackToStrings(static_cast<listpack>(obj->ptr));
    while (count > 0 && !items.empty())
    {
        if (from_head)
        {
            popped.push_back(items.front());
            items.erase(items.begin());
        }
        else
        {
            popped.push_back(items.back());
            items.pop_back();
        }

        --count;
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = stringsToListpack(items);
    return popped;
}

vector<string> listRange(const RedisObject* obj, long long start, long long stop)
{
    vector<string> items;
    if (obj->encoding == ENC_LISTPACK)
    {
        items = listpackToStrings(static_cast<listpack>(obj->ptr));
    }
    else
    {
        const RedisList* list = listAsList(const_cast<RedisObject*>(obj));
        items.assign(list->begin(), list->end());
    }

    const long long size = static_cast<long long>(items.size());
    if (size == 0)
    {
        return {};
    }

    start = normalizeIndex(start, size);
    stop = normalizeIndex(stop, size);
    if (start < 0)
    {
        start = 0;
    }
    if (stop >= size)
    {
        stop = size - 1;
    }
    if (start > stop)
    {
        return {};
    }

    return vector<string>(items.begin() + start, items.begin() + stop + 1);
}

bool listIndex(const RedisObject* obj, long long index, string& out)
{
    const vector<string> items = obj->encoding == ENC_LISTPACK
        ? listpackToStrings(static_cast<listpack>(obj->ptr))
        : vector<string>(listAsList(const_cast<RedisObject*>(obj))->begin(), listAsList(const_cast<RedisObject*>(obj))->end());

    const long long size = static_cast<long long>(items.size());
    index = normalizeIndex(index, size);
    if (index < 0 || index >= size)
    {
        return false;
    }

    out = items[static_cast<size_t>(index)];
    return true;
}

bool listSet(RedisObject* obj, long long index, const string& value)
{
    vector<string> items = obj->encoding == ENC_LISTPACK
        ? listpackToStrings(static_cast<listpack>(obj->ptr))
        : vector<string>(listAsList(obj)->begin(), listAsList(obj)->end());

    const long long size = static_cast<long long>(items.size());
    index = normalizeIndex(index, size);
    if (index < 0 || index >= size)
    {
        return false;
    }

    items[static_cast<size_t>(index)] = value;
    listMaybePromote(obj, items);
    if (obj->encoding == ENC_QUICKLIST)
    {
        *listAsList(obj) = RedisList(items.begin(), items.end());
        return true;
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = stringsToListpack(items);
    return true;
}

long long listInsert(RedisObject* obj, bool before, const string& pivot, const string& value)
{
    vector<string> items = obj->encoding == ENC_LISTPACK
        ? listpackToStrings(static_cast<listpack>(obj->ptr))
        : vector<string>(listAsList(obj)->begin(), listAsList(obj)->end());

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i] != pivot)
        {
            continue;
        }

        const size_t pos = before ? i : i + 1;
        items.insert(items.begin() + static_cast<long long>(pos), value);
        listMaybePromote(obj, items);
        if (obj->encoding == ENC_QUICKLIST)
        {
            *listAsList(obj) = RedisList(items.begin(), items.end());
        }
        else
        {
            lpFree(static_cast<listpack>(obj->ptr));
            obj->ptr = stringsToListpack(items);
        }

        return static_cast<long long>(items.size());
    }

    return 0;
}

long long listRem(RedisObject* obj, long long count, const string& value)
{
    vector<string> items = obj->encoding == ENC_LISTPACK
        ? listpackToStrings(static_cast<listpack>(obj->ptr))
        : vector<string>(listAsList(obj)->begin(), listAsList(obj)->end());

    long long removed = 0;
    if (count == 0)
    {
        const size_t before = items.size();
        items.erase(remove(items.begin(), items.end(), value), items.end());
        removed = static_cast<long long>(before - items.size());
    }
    else if (count > 0)
    {
        for (auto it = items.begin(); it != items.end() && count > 0;)
        {
            if (*it == value)
            {
                it = items.erase(it);
                ++removed;
                --count;
            }
            else
            {
                ++it;
            }
        }
    }
    else
    {
        long long abs_count = -count;
        for (long long i = static_cast<long long>(items.size()) - 1; i >= 0 && abs_count > 0; --i)
        {
            if (items[static_cast<size_t>(i)] == value)
            {
                items.erase(items.begin() + i);
                ++removed;
                --abs_count;
            }
        }
    }

    if (obj->encoding == ENC_QUICKLIST)
    {
        RedisList rebuilt(items.begin(), items.end());
        *listAsList(obj) = std::move(rebuilt);
        return removed;
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = stringsToListpack(items);
    return removed;
}

void listTrim(RedisObject* obj, long long start, long long stop)
{
    vector<string> trimmed = listRange(obj, start, stop);
    if (obj->encoding == ENC_QUICKLIST)
    {
        *listAsList(obj) = RedisList(trimmed.begin(), trimmed.end());
        return;
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = stringsToListpack(trimmed);
}

long long setAdd(RedisObject* obj, const vector<string>& members)
{
    long long added = 0;

    if (obj->encoding == ENC_HASHTABLE)
    {
        RedisSet* set = setAsSet(obj);
        for (const string& member : members)
        {
            if (set->insert(member).second)
            {
                ++added;
            }
        }

        return added;
    }

    if (obj->encoding == ENC_INTSET)
    {
        intset is = static_cast<intset>(obj->ptr);
        for (const string& member : members)
        {
            long long value = 0;
            if (!tryParseInteger(member, value))
            {
                vector<string> all = intsetToStrings(is);
                for (const string& candidate : members)
                {
                    if (find(all.begin(), all.end(), candidate) == all.end())
                    {
                        all.push_back(candidate);
                        ++added;
                    }
                }

                promoteSetToListpack(obj, all);
                setMaybePromote(obj, all);
                return added;
            }
        }

        for (const string& member : members)
        {
            long long value = stoll(member);
            int success = 0;
            is = intsetAdd(is, value, &success);
            added += success;
        }

        obj->ptr = is;
        setMaybePromote(obj, intsetToStrings(is));
        return added;
    }

    vector<string> all = listpackToStrings(static_cast<listpack>(obj->ptr));
    for (const string& member : members)
    {
        if (find(all.begin(), all.end(), member) == all.end())
        {
            all.push_back(member);
            ++added;
        }
    }

    setMaybePromote(obj, all);
    if (obj->encoding == ENC_HASHTABLE)
    {
        RedisSet* set = setAsSet(obj);
        for (const string& member : members)
        {
            if (set->insert(member).second)
            {
                ++added;
            }
        }

        return added;
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = stringsToListpack(all);
    return added;
}

long long setRem(RedisObject* obj, const vector<string>& members)
{
    if (obj->encoding == ENC_HASHTABLE)
    {
        RedisSet* set = setAsSet(obj);
        long long removed = 0;
        for (const string& member : members)
        {
            removed += static_cast<long long>(set->erase(member));
        }

        return removed;
    }

    if (obj->encoding == ENC_INTSET)
    {
        intset is = static_cast<intset>(obj->ptr);
        long long removed = 0;
        for (const string& member : members)
        {
            long long value = 0;
            if (!tryParseInteger(member, value))
            {
                continue;
            }

            int success = 0;
            is = intsetRemove(is, value, &success);
            removed += success;
        }

        obj->ptr = is;
        return removed;
    }

    vector<string> all = listpackToStrings(static_cast<listpack>(obj->ptr));
    long long removed = 0;
    for (const string& member : members)
    {
        const auto it = find(all.begin(), all.end(), member);
        if (it != all.end())
        {
            all.erase(it);
            ++removed;
        }
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = stringsToListpack(all);
    return removed;
}

long long setCard(const RedisObject* obj)
{
    if (obj->encoding == ENC_INTSET)
    {
        return static_cast<long long>(intsetLen(static_cast<intset>(obj->ptr)));
    }

    if (obj->encoding == ENC_LISTPACK)
    {
        return static_cast<long long>(lpLength(static_cast<listpack>(obj->ptr)));
    }

    return static_cast<long long>(setAsSet(const_cast<RedisObject*>(obj))->size());
}

bool setIsMember(const RedisObject* obj, const string& member)
{
    if (obj->encoding == ENC_HASHTABLE)
    {
        return setAsSet(const_cast<RedisObject*>(obj))->count(member) > 0;
    }

    if (obj->encoding == ENC_INTSET)
    {
        long long value = 0;
        if (!tryParseInteger(member, value))
        {
            return false;
        }

        return intsetFind(static_cast<intset>(obj->ptr), value) >= 0;
    }

    const vector<string> all = listpackToStrings(static_cast<listpack>(obj->ptr));
    return find(all.begin(), all.end(), member) != all.end();
}

vector<string> setMembers(const RedisObject* obj)
{
    if (obj->encoding == ENC_HASHTABLE)
    {
        const RedisSet* set = setAsSet(const_cast<RedisObject*>(obj));
        return vector<string>(set->begin(), set->end());
    }

    if (obj->encoding == ENC_INTSET)
    {
        return intsetToStrings(static_cast<intset>(obj->ptr));
    }

    return listpackToStrings(static_cast<listpack>(obj->ptr));
}

void setReplaceMembers(RedisObject* obj, const vector<string>& members)
{
    freeObjectPayload(obj);
    if (members.empty())
    {
        obj->encoding = ENC_INTSET;
        obj->ptr = intsetNew();
        return;
    }

    bool all_integers = true;
    for (const string& member : members)
    {
        long long value = 0;
        if (!tryParseInteger(member, value))
        {
            all_integers = false;
            break;
        }
    }

    if (all_integers && members.size() <= INTSET_MAX_ENTRIES)
    {
        intset is = intsetNew();
        for (const string& member : members)
        {
            long long value = stoll(member);
            int success = 0;
            is = intsetAdd(is, value, &success);
        }

        obj->encoding = ENC_INTSET;
        obj->ptr = is;
        return;
    }

    if (!stringsNeedPromotion(members))
    {
        obj->encoding = ENC_LISTPACK;
        obj->ptr = stringsToListpack(members);
        return;
    }

    obj->encoding = ENC_HASHTABLE;
    obj->ptr = new RedisSet(members.begin(), members.end());
}

long long zsetCard(const RedisObject* obj)
{
    if (obj->encoding == ENC_LISTPACK)
    {
        return static_cast<long long>(listpackToZSetPairs(static_cast<listpack>(obj->ptr)).size());
    }

    return static_cast<long long>(zsetAsZSet(const_cast<RedisObject*>(obj))->scores.size());
}

bool zsetScore(const RedisObject* obj, const string& member, double& out)
{
    if (obj->encoding == ENC_LISTPACK)
    {
        for (const auto& pair : listpackToZSetPairs(static_cast<listpack>(obj->ptr)))
        {
            if (pair.second == member)
            {
                out = pair.first;
                return true;
            }
        }

        return false;
    }

    const ZSet* zset = zsetAsZSet(const_cast<RedisObject*>(obj));
    auto it = zset->scores.find(member);
    if (it == zset->scores.end())
    {
        return false;
    }

    out = it->second;
    return true;
}

long long zsetAdd(
    RedisObject* obj,
    const vector<pair<double, string>>& entries,
    bool nx,
    bool xx,
    bool gt,
    bool lt,
    bool ch,
    long long& added,
    long long& changed)
{
    added = 0;
    changed = 0;

    if (obj->encoding == ENC_SKIPLIST)
    {
        ZSet* zset = zsetAsZSet(obj);
        for (const auto& entry : entries)
        {
            auto old = zset->scores.find(entry.second);
            const bool exists = old != zset->scores.end();
            if ((nx && exists) || (xx && !exists))
            {
                continue;
            }
            if (exists && ((gt && entry.first <= old->second) || (lt && entry.first >= old->second)))
            {
                continue;
            }

            if (!exists)
            {
                ++added;
            }
            if (!exists || old->second != entry.first)
            {
                ++changed;
            }

            zset->scores[entry.second] = entry.first;
            zset->index.insert(entry.first, entry.second);
        }

        return ch ? changed : added;
    }

    vector<pair<double, string>> pairs = listpackToZSetPairs(static_cast<listpack>(obj->ptr));
    for (const auto& entry : entries)
    {
        bool exists = false;
        size_t index = 0;
        for (; index < pairs.size(); ++index)
        {
            if (pairs[index].second == entry.second)
            {
                exists = true;
                break;
            }
        }

        if ((nx && exists) || (xx && !exists))
        {
            continue;
        }
        if (exists && ((gt && entry.first <= pairs[index].first) || (lt && entry.first >= pairs[index].first)))
        {
            continue;
        }

        if (!exists)
        {
            ++added;
            pairs.push_back(entry);
        }
        else
        {
            if (pairs[index].first != entry.first)
            {
                ++changed;
            }
            pairs[index].first = entry.first;
        }
    }

    zsetMaybePromote(obj, pairs);
    if (obj->encoding == ENC_SKIPLIST)
    {
        return ch ? changed : added;
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = zsetPairsToListpack(pairs);
    return ch ? changed : added;
}

long long zsetRem(RedisObject* obj, const vector<string>& members)
{
    if (obj->encoding == ENC_SKIPLIST)
    {
        ZSet* zset = zsetAsZSet(obj);
        long long removed = 0;
        for (const string& member : members)
        {
            if (zset->index.remove(member))
            {
                zset->scores.erase(member);
                ++removed;
            }
        }

        return removed;
    }

    vector<pair<double, string>> pairs = listpackToZSetPairs(static_cast<listpack>(obj->ptr));
    long long removed = 0;
    for (const string& member : members)
    {
        const size_t before = pairs.size();
        pairs.erase(
            remove_if(pairs.begin(), pairs.end(), [&](const pair<double, string>& entry) {
                return entry.second == member;
            }),
            pairs.end());
        if (pairs.size() != before)
        {
            ++removed;
        }
    }

    lpFree(static_cast<listpack>(obj->ptr));
    obj->ptr = zsetPairsToListpack(pairs);
    return removed;
}

long long zsetRank(const RedisObject* obj, const string& member, bool reverse, bool& found)
{
    found = false;
    if (obj->encoding == ENC_SKIPLIST)
    {
        const long long rank = zsetAsZSet(const_cast<RedisObject*>(obj))->index.rank(member, reverse);
        if (rank >= 0)
        {
            found = true;
        }

        return rank;
    }

    vector<pair<double, string>> pairs = listpackToZSetPairs(static_cast<listpack>(obj->ptr));
    sort(pairs.begin(), pairs.end(), [](const pair<double, string>& a, const pair<double, string>& b) {
        if (a.first != b.first)
        {
            return a.first < b.first;
        }

        return a.second < b.second;
    });

    if (reverse)
    {
        std::reverse(pairs.begin(), pairs.end());
    }

    for (size_t i = 0; i < pairs.size(); ++i)
    {
        if (pairs[i].second == member)
        {
            found = true;
            return static_cast<long long>(i);
        }
    }

    return -1;
}

long long zsetCount(const RedisObject* obj, double min, double max)
{
    if (obj->encoding == ENC_SKIPLIST)
    {
        return static_cast<long long>(zsetAsZSet(const_cast<RedisObject*>(obj))->index.countByScore(min, max));
    }

    long long count = 0;
    for (const auto& pair : listpackToZSetPairs(static_cast<listpack>(obj->ptr)))
    {
        if (pair.first >= min && pair.first <= max)
        {
            ++count;
        }
    }

    return count;
}

vector<ZSetEntry> zsetRangeByRank(const RedisObject* obj, long long start, long long stop, bool reverse)
{
    if (obj->encoding == ENC_SKIPLIST)
    {
        return zsetAsZSet(const_cast<RedisObject*>(obj))->index.rangeByRank(start, stop, reverse);
    }

    vector<ZSetEntry> entries;
    for (const auto& pair : listpackToZSetPairs(static_cast<listpack>(obj->ptr)))
    {
        entries.push_back(ZSetEntry{pair.second, pair.first});
    }

    sort(entries.begin(), entries.end(), [](const ZSetEntry& a, const ZSetEntry& b) {
        if (a.score != b.score)
        {
            return a.score < b.score;
        }

        return a.member < b.member;
    });

    if (reverse)
    {
        std::reverse(entries.begin(), entries.end());
    }

    const long long size = static_cast<long long>(entries.size());
    if (size == 0)
    {
        return {};
    }

    start = normalizeIndex(start, size);
    stop = normalizeIndex(stop, size);
    if (start < 0)
    {
        start = 0;
    }
    if (stop >= size)
    {
        stop = size - 1;
    }
    if (start > stop)
    {
        return {};
    }

    return vector<ZSetEntry>(entries.begin() + start, entries.begin() + stop + 1);
}

vector<ZSetEntry> zsetRangeByScore(
    const RedisObject* obj,
    double min,
    double max,
    bool reverse,
    long long offset,
    long long count)
{
    if (obj->encoding == ENC_SKIPLIST)
    {
        return zsetAsZSet(const_cast<RedisObject*>(obj))->index.rangeByScore(min, max, reverse, offset, count);
    }

    vector<ZSetEntry> entries;
    for (const auto& pair : listpackToZSetPairs(static_cast<listpack>(obj->ptr)))
    {
        if (pair.first >= min && pair.first <= max)
        {
            entries.push_back(ZSetEntry{pair.second, pair.first});
        }
    }

    sort(entries.begin(), entries.end(), [](const ZSetEntry& a, const ZSetEntry& b) {
        if (a.score != b.score)
        {
            return a.score < b.score;
        }

        return a.member < b.member;
    });

    if (reverse)
    {
        std::reverse(entries.begin(), entries.end());
    }

    if (offset < 0)
    {
        offset = 0;
    }

    if (static_cast<size_t>(offset) >= entries.size())
    {
        return {};
    }

    const size_t begin = static_cast<size_t>(offset);
    size_t end = entries.size();
    if (count >= 0)
    {
        end = std::min(entries.size(), begin + static_cast<size_t>(count));
    }

    return vector<ZSetEntry>(entries.begin() + begin, entries.begin() + end);
}

double zsetIncrBy(RedisObject* obj, const string& member, double increment)
{
    double old = 0;
    zsetScore(obj, member, old);
    const double next = old + increment;
    long long added = 0;
    long long changed = 0;
    zsetAdd(obj, {{next, member}}, false, false, false, false, false, added, changed);
    return next;
}

vector<ZSetEntry> zsetPop(RedisObject* obj, long long count, bool max_side)
{
    vector<ZSetEntry> entries = zsetRangeByRank(obj, 0, count - 1, max_side);
    vector<string> members;
    members.reserve(entries.size());
    for (const ZSetEntry& entry : entries)
    {
        members.push_back(entry.member);
    }

    zsetRem(obj, members);
    return entries;
}
