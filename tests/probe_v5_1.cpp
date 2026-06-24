#include "listpack.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

int main()
{
    listpack lp = lpNew();
    assert(lp != nullptr);
    assert(lpLength(lp) == 0);
    assert(lpFirst(lp) == nullptr);

    lp = lpAppendInteger(lp, 42);
    assert(lpLength(lp) == 1);

    lp = lpAppend(lp, reinterpret_cast<const unsigned char*>("hello"), 5);
    assert(lpLength(lp) == 2);

    const char binary[] = {'a', '\0', 'b'};
    lp = lpAppend(lp, reinterpret_cast<const unsigned char*>(binary), sizeof(binary));
    assert(lpLength(lp) == 3);

    std::vector<std::string> forward;
    for (listpackEntry entry = lpFirst(lp); entry != nullptr; entry = lpNext(lp, entry))
    {
        long long val = 0;
        unsigned char* sval = nullptr;
        uint32_t slen = 0;
        const int kind = lpGet(entry, &val, &sval, &slen);
        if (kind == 1)
        {
            forward.push_back(std::to_string(val));
        }
        else if (kind == 0)
        {
            forward.push_back(std::string(reinterpret_cast<char*>(sval), slen));
        }
    }

    assert(forward.size() == 3);
    assert(forward[0] == "42");
    assert(forward[1] == "hello");
    assert(forward[2].size() == 3);
    assert(forward[2][1] == '\0');

    std::vector<std::string> backward;
    for (listpackEntry entry = lpLast(lp); entry != nullptr; entry = lpPrev(lp, entry))
    {
        long long val = 0;
        unsigned char* sval = nullptr;
        uint32_t slen = 0;
        const int kind = lpGet(entry, &val, &sval, &slen);
        if (kind == 1)
        {
            backward.push_back(std::to_string(val));
        }
        else if (kind == 0)
        {
            backward.push_back(std::string(reinterpret_cast<char*>(sval), slen));
        }
    }

    assert(backward.size() == 3);
    assert(backward[0] == forward[2]);
    assert(backward[2] == forward[0]);

    lpFree(lp);

    lp = lpNew();
    std::string long_value(100, 'x');
    lp = lpAppend(lp, reinterpret_cast<const unsigned char*>(long_value.data()), long_value.size());
    assert(lpLength(lp) == 1);

    listpackEntry only = lpFirst(lp);
    long long val = 0;
    unsigned char* sval = nullptr;
    uint32_t slen = 0;
    assert(lpGet(only, &val, &sval, &slen) == 0);
    assert(slen == 100);
    assert(std::memcmp(sval, long_value.data(), slen) == 0);

    lpFree(lp);
}
