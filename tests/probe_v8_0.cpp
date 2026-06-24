#include "encoding.h"
#include "object.h"
#include "rdb.h"

#include <cassert>
#include <cstdio>
#include <vector>

int main()
{
    std::vector<RedisDb> dbs(2);
    RedisDb& db = dbs[0];
    db.data["foo"] = createStringObject("bar");
    db.data["count"] = createStringObject("42");
    db.data["hash"] = createHashObject();
    bool added = false;
    hashSet(db.data["hash"], "name", "Alice", added);

    const char* path = "tests_probe_v8.rdb";
    assert(saveRDB(path, dbs));

    std::vector<RedisDb> loaded(2);
    assert(loadRDB(path, loaded));
    assert(loaded[0].data.size() == 3);
    assert(getStringValue(loaded[0].data["foo"]) == "bar");
    assert(getStringValue(loaded[0].data["count"]) == "42");

    std::string value;
    assert(hashGet(loaded[0].data["hash"], "name", value));
    assert(value == "Alice");

    std::remove(path);
    return 0;
}
