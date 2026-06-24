#include "intset.h"

#include <cassert>
#include <cstddef>
#include <vector>

int main()
{
    intset is = intsetNew();
    assert(is != nullptr);
    assert(intsetLen(is) == 0);
    assert(intsetEncoding(is) == INTSET_ENC_INT16);

    int success = 0;
    is = intsetAdd(is, 30, &success);
    assert(success == 1);
    is = intsetAdd(is, 10, &success);
    assert(success == 1);
    is = intsetAdd(is, 20, &success);
    assert(success == 1);
    is = intsetAdd(is, 20, &success);
    assert(success == 0);
    assert(intsetLen(is) == 3);
    assert(intsetGet(is, 0) == 10);
    assert(intsetGet(is, 1) == 20);
    assert(intsetGet(is, 2) == 30);
    assert(intsetFind(is, 20) == 1);
    assert(intsetFind(is, 99) == -1);

    is = intsetAdd(is, 40000, &success);
    assert(success == 1);
    assert(intsetEncoding(is) == INTSET_ENC_INT32);
    assert(intsetLen(is) == 4);
    assert(intsetGet(is, 3) == 40000);

    is = intsetAdd(is, static_cast<int64_t>(1) << 40, &success);
    assert(success == 1);
    assert(intsetEncoding(is) == INTSET_ENC_INT64);
    assert(intsetGet(is, 4) == (static_cast<int64_t>(1) << 40));

    is = intsetRemove(is, 20, &success);
    assert(success == 1);
    assert(intsetLen(is) == 4);
    assert(intsetFind(is, 20) == -1);
    assert(intsetGet(is, 1) == 30);

    is = intsetRemove(is, 999, &success);
    assert(success == 0);

    std::vector<int64_t> remaining;
    for (uint32_t i = 0; i < intsetLen(is); ++i)
    {
        remaining.push_back(intsetGet(is, i));
    }

    assert(remaining.size() == 4);
    for (size_t i = 1; i < remaining.size(); ++i)
    {
        assert(remaining[i - 1] < remaining[i]);
    }

    intsetFree(is);
}
