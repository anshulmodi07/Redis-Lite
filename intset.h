#pragma once

#include <cstdint>

constexpr uint32_t INTSET_ENC_INT16 = 2;
constexpr uint32_t INTSET_ENC_INT32 = 4;
constexpr uint32_t INTSET_ENC_INT64 = 8;

struct IntSet
{
    uint32_t encoding;
    uint32_t length;
};

using intset = IntSet*;

intset intsetNew();
void intsetFree(intset is);
uint32_t intsetLen(const intset is);
uint32_t intsetEncoding(const intset is);

int64_t intsetGet(const intset is, uint32_t pos);
int32_t intsetFind(const intset is, int64_t value);

intset intsetAdd(intset is, int64_t value, int* success);
intset intsetRemove(intset is, int64_t value, int* success);
