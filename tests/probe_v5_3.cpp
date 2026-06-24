#include "encoding.h"
#include "object.h"

#include <cassert>
#include <string>
#include <vector>

using namespace std;

int main()
{
    RedisObject* hash = createHashObject();
    assert(hash->encoding == ENC_LISTPACK);
    bool added = false;
    hashSet(hash, "a", "1", added);
    assert(added);
    hashSet(hash, "b", "2", added);
    assert(added);
    assert(objectEncodingName(hash) == "listpack");

    for (int i = 3; i <= 130; ++i)
    {
        hashSet(hash, "field" + to_string(i), to_string(i), added);
    }
    assert(hash->encoding == ENC_HASHTABLE);
    assert(objectEncodingName(hash) == "hashtable");
    destroyObject(hash);

    RedisObject* set = createSetObject();
    assert(set->encoding == ENC_INTSET);
    setAdd(set, {"1", "2", "3"});
    assert(objectEncodingName(set) == "intset");
    setAdd(set, {"alpha"});
    assert(set->encoding == ENC_LISTPACK);
    assert(objectEncodingName(set) == "listpack");
    destroyObject(set);

    RedisObject* list = createListObject();
    listPushBack(list, {"small"});
    assert(objectEncodingName(list) == "listpack");
    vector<string> many;
    for (int i = 0; i < 129; ++i)
    {
        many.push_back("item" + to_string(i));
    }
    listPushBack(list, many);
    assert(list->encoding == ENC_QUICKLIST);
    assert(objectEncodingName(list) == "quicklist");
    destroyObject(list);

    return 0;
}
