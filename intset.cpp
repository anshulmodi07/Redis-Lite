#include "intset.h"

#include <climits>
#include <cstdlib>
#include <cstring>

namespace
{
constexpr int64_t INT16_MIN_VAL = INT16_MIN;
constexpr int64_t INT16_MAX_VAL = INT16_MAX;
constexpr int64_t INT32_MIN_VAL = INT32_MIN;
constexpr int64_t INT32_MAX_VAL = INT32_MAX;

uint8_t* intsetContents(intset is)
{
    return reinterpret_cast<uint8_t*>(is) + sizeof(IntSet);
}

uint32_t valueEncoding(int64_t value)
{
    if (value < INT32_MIN_VAL || value > INT32_MAX_VAL)
    {
        return INTSET_ENC_INT64;
    }

    if (value < INT16_MIN_VAL || value > INT16_MAX_VAL)
    {
        return INTSET_ENC_INT32;
    }

    return INTSET_ENC_INT16;
}

bool valueFitsEncoding(int64_t value, uint32_t encoding)
{
    return valueEncoding(value) <= encoding;
}

int64_t readEncoded(const uint8_t* ptr, uint32_t encoding)
{
    switch (encoding)
    {
    case INTSET_ENC_INT16:
    {
        int16_t value = 0;
        std::memcpy(&value, ptr, sizeof(value));
        return value;
    }
    case INTSET_ENC_INT32:
    {
        int32_t value = 0;
        std::memcpy(&value, ptr, sizeof(value));
        return value;
    }
    case INTSET_ENC_INT64:
    {
        int64_t value = 0;
        std::memcpy(&value, ptr, sizeof(value));
        return value;
    }
    default:
        return 0;
    }
}

void writeEncoded(uint8_t* ptr, uint32_t encoding, int64_t value)
{
    switch (encoding)
    {
    case INTSET_ENC_INT16:
    {
        const auto encoded = static_cast<int16_t>(value);
        std::memcpy(ptr, &encoded, sizeof(encoded));
        break;
    }
    case INTSET_ENC_INT32:
    {
        const auto encoded = static_cast<int32_t>(value);
        std::memcpy(ptr, &encoded, sizeof(encoded));
        break;
    }
    case INTSET_ENC_INT64:
    {
        std::memcpy(ptr, &value, sizeof(value));
        break;
    }
    default:
        break;
    }
}

intset resize(intset is, uint32_t new_length, uint32_t encoding)
{
    const size_t new_size = sizeof(IntSet) + static_cast<size_t>(new_length) * encoding;
    is = static_cast<intset>(std::realloc(is, new_size));
    if (is == nullptr)
    {
        return nullptr;
    }

    is->encoding = encoding;
    is->length = new_length;
    return is;
}

bool search(const intset is, int64_t value, uint32_t* pos)
{
    uint32_t min = 0;
    uint32_t max = is->length - 1;
    uint8_t* contents = intsetContents(is);

    if (is->length == 0)
    {
        *pos = 0;
        return false;
    }

    if (value > readEncoded(contents + static_cast<size_t>(max) * is->encoding, is->encoding))
    {
        *pos = is->length;
        return false;
    }

    if (value < readEncoded(contents, is->encoding))
    {
        *pos = 0;
        return false;
    }

    while (max >= min)
    {
        const uint32_t mid = (min + max) / 2;
        const int64_t current = readEncoded(contents + static_cast<size_t>(mid) * is->encoding, is->encoding);

        if (current > value)
        {
            max = mid - 1;
        }
        else if (current < value)
        {
            min = mid + 1;
        }
        else
        {
            *pos = mid;
            return true;
        }
    }

    *pos = min;
    return false;
}

intset upgradeAndAdd(intset is, int64_t value)
{
    const uint32_t old_length = is->length;
    const uint32_t old_encoding = is->encoding;
    const uint32_t new_encoding = valueEncoding(value);

    intset upgraded = static_cast<intset>(std::malloc(sizeof(IntSet) + static_cast<size_t>(old_length + 1) * new_encoding));
    if (upgraded == nullptr)
    {
        return nullptr;
    }

    upgraded->encoding = new_encoding;
    upgraded->length = old_length + 1;

    uint32_t insert_pos = 0;
    search(is, value, &insert_pos);

    uint8_t* old_contents = intsetContents(is);
    uint8_t* new_contents = intsetContents(upgraded);
    uint32_t out = 0;

    for (uint32_t i = 0; i < old_length; ++i)
    {
        if (out == insert_pos)
        {
            writeEncoded(new_contents + static_cast<size_t>(out) * new_encoding, new_encoding, value);
            ++out;
        }

        const int64_t current = readEncoded(old_contents + static_cast<size_t>(i) * old_encoding, old_encoding);
        writeEncoded(new_contents + static_cast<size_t>(out) * new_encoding, new_encoding, current);
        ++out;
    }

    if (insert_pos == old_length)
    {
        writeEncoded(new_contents + static_cast<size_t>(out) * new_encoding, new_encoding, value);
    }

    intsetFree(is);
    return upgraded;
}
}

intset intsetNew()
{
    intset is = static_cast<intset>(std::malloc(sizeof(IntSet)));
    if (is == nullptr)
    {
        return nullptr;
    }

    is->encoding = INTSET_ENC_INT16;
    is->length = 0;
    return is;
}

void intsetFree(intset is)
{
    std::free(is);
}

uint32_t intsetLen(const intset is)
{
    return is->length;
}

uint32_t intsetEncoding(const intset is)
{
    return is->encoding;
}

int64_t intsetGet(const intset is, uint32_t pos)
{
    return readEncoded(intsetContents(is) + static_cast<size_t>(pos) * is->encoding, is->encoding);
}

int32_t intsetFind(const intset is, int64_t value)
{
    if (is->length == 0)
    {
        return -1;
    }

    uint32_t pos = 0;
    if (search(is, value, &pos))
    {
        return static_cast<int32_t>(pos);
    }

    return -1;
}

intset intsetAdd(intset is, int64_t value, int* success)
{
    if (success != nullptr)
    {
        *success = 0;
    }

    if (!valueFitsEncoding(value, is->encoding))
    {
        is = upgradeAndAdd(is, value);
        if (is == nullptr)
        {
            return nullptr;
        }

        if (success != nullptr)
        {
            *success = 1;
        }

        return is;
    }

    uint32_t pos = 0;
    if (search(is, value, &pos))
    {
        return is;
    }

    const uint32_t old_length = is->length;
    is = resize(is, old_length + 1, is->encoding);
    if (is == nullptr)
    {
        return nullptr;
    }

    uint8_t* contents = intsetContents(is);
    if (pos < old_length)
    {
        std::memmove(
            contents + static_cast<size_t>(pos + 1) * is->encoding,
            contents + static_cast<size_t>(pos) * is->encoding,
            static_cast<size_t>(old_length - pos) * is->encoding);
    }

    writeEncoded(contents + static_cast<size_t>(pos) * is->encoding, is->encoding, value);

    if (success != nullptr)
    {
        *success = 1;
    }

    return is;
}

intset intsetRemove(intset is, int64_t value, int* success)
{
    if (success != nullptr)
    {
        *success = 0;
    }

    uint32_t pos = 0;
    if (!search(is, value, &pos))
    {
        return is;
    }

    uint8_t* contents = intsetContents(is);
    const uint32_t tail = is->length - pos - 1;
    if (tail > 0)
    {
        std::memmove(
            contents + static_cast<size_t>(pos) * is->encoding,
            contents + static_cast<size_t>(pos + 1) * is->encoding,
            static_cast<size_t>(tail) * is->encoding);
    }

    is = resize(is, is->length - 1, is->encoding);

    if (success != nullptr)
    {
        *success = 1;
    }

    return is;
}
