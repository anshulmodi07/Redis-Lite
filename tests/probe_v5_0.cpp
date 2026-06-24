#include "object.h"
#include "sds.h"

#include <cassert>
#include <cstring>
#include <string>

int main()
{
    sds s = sdsnew("hello");
    assert(sdslen(s) == 5);
    assert(std::strcmp(s, "hello") == 0);

    s = sdscat(s, " world");
    assert(sdslen(s) == 11);

    s = sdsgrow(s, 100);
    assert(sdslen(s) == 11);

    const char payload[] = {'a', '\0', 'b'};
    sdsfree(s);
    s = sdsnewlen(payload, sizeof(payload));
    assert(sdslen(s) == 3);
    assert(s[1] == '\0');
    sdsfree(s);

    RedisObject* obj = createStringObject("abc");
    assert(obj->encoding == ENC_RAW);
    assert(getStringValue(obj) == "abc");
    assert(stringObjectLength(obj) == 3);

    appendStringValue(obj, "def");
    assert(getStringValue(obj) == "abcdef");
    destroyObject(obj);

    RedisObject* counter = createStringObject("10");
    appendStringValue(counter, "5");
    assert(counter->encoding == ENC_INT);
    long long value = 0;
    assert(readStringInteger(counter, value));
    assert(value == 105);
    destroyObject(counter);
}
