#pragma once

#include <cstdint>

using listpack = unsigned char*;
using listpackEntry = unsigned char*;

constexpr unsigned char LP_EOF = 0xFF;

listpack lpNew();
void lpFree(listpack lp);
uint32_t lpLength(listpack lp);
uint32_t lpBytes(listpack lp);

listpackEntry lpFirst(listpack lp);
listpackEntry lpLast(listpack lp);
listpackEntry lpNext(listpack lp, listpackEntry entry);
listpackEntry lpPrev(listpack lp, listpackEntry entry);

listpack lpAppend(listpack lp, const unsigned char* data, uint32_t len);
listpack lpAppendInteger(listpack lp, long long value);

// Returns 1 for integer, 0 for string, -1 on error.
int lpGet(
    listpackEntry entry,
    long long* val,
    unsigned char** sval,
    uint32_t* slen);
