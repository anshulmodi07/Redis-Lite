#include "listpack.h"

#include <cstdlib>
#include <cstring>

namespace
{
constexpr uint32_t LP_HDR_SIZE = 6;
constexpr unsigned char LP_BACKLEN_MAX_1BYTE = 254;

uint32_t readU32Le(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

void writeU32Le(unsigned char* p, uint32_t value)
{
    p[0] = static_cast<unsigned char>(value & 0xFF);
    p[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    p[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    p[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
}

uint16_t readU16Le(const unsigned char* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

void writeU16Le(unsigned char* p, uint16_t value)
{
    p[0] = static_cast<unsigned char>(value & 0xFF);
    p[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
}

uint32_t encodeBacklen(uint32_t len, unsigned char* out)
{
    if (len < LP_BACKLEN_MAX_1BYTE)
    {
        out[0] = static_cast<unsigned char>(len);
        return 1;
    }

    out[0] = LP_BACKLEN_MAX_1BYTE;
    writeU32Le(out + 1, len);
    return 5;
}

uint32_t readBacklen(const unsigned char* entry, uint32_t* out)
{
    if (entry[0] == LP_BACKLEN_MAX_1BYTE)
    {
        *out = readU32Le(entry + 1);
        return 5;
    }

    *out = entry[0];
    return 1;
}

bool isUint7(unsigned char byte)
{
    return (byte & 0x80U) == 0;
}

bool isString6(unsigned char byte)
{
    return (byte & 0xC0U) == 0x80U;
}

uint32_t encodedSize(const unsigned char* enc)
{
    const unsigned char byte = enc[0];

    if (isUint7(byte))
    {
        return 1;
    }

    if (isString6(byte))
    {
        return 1U + (byte & 0x3FU);
    }

    if (byte == 0xE0U)
    {
        const uint32_t len = readU32Le(enc + 1);
        return 5U + len;
    }

    if (byte == 0xF0U)
    {
        return 9;
    }

    return 0;
}

uint32_t entryTotalSize(const unsigned char* entry)
{
    uint32_t backlen = 0;
    const uint32_t backlen_size = readBacklen(entry, &backlen);
    return backlen_size + encodedSize(entry + backlen_size);
}

uint32_t encodeString(const unsigned char* data, uint32_t len, unsigned char* out)
{
    if (len <= 63)
    {
        out[0] = static_cast<unsigned char>(0x80U | len);
        if (len > 0)
        {
            std::memcpy(out + 1, data, len);
        }
        return 1U + len;
    }

    out[0] = 0xE0U;
    writeU32Le(out + 1, len);
    std::memcpy(out + 5, data, len);
    return 5U + len;
}

uint32_t encodeInteger(long long value, unsigned char* out)
{
    if (value >= 0 && value <= 127)
    {
        out[0] = static_cast<unsigned char>(value);
        return 1;
    }

    out[0] = 0xF0U;
    std::memcpy(out + 1, &value, sizeof(value));
    return 9;
}

void setNumElements(listpack lp, uint16_t count)
{
    writeU16Le(lp + 4, count);
}

void setTotalBytes(listpack lp, uint32_t bytes)
{
    writeU32Le(lp, bytes);
}

uint32_t payloadEndOffset(listpack lp)
{
    return lpBytes(lp) - 1;
}
}

listpack lpNew()
{
    listpack lp = static_cast<listpack>(std::malloc(LP_HDR_SIZE + 1));
    if (lp == nullptr)
    {
        return nullptr;
    }

    setTotalBytes(lp, LP_HDR_SIZE + 1);
    setNumElements(lp, 0);
    lp[LP_HDR_SIZE] = LP_EOF;
    return lp;
}

void lpFree(listpack lp)
{
    std::free(lp);
}

uint32_t lpLength(listpack lp)
{
    return readU16Le(lp + 4);
}

uint32_t lpBytes(listpack lp)
{
    return readU32Le(lp);
}

listpackEntry lpFirst(listpack lp)
{
    if (lpLength(lp) == 0)
    {
        return nullptr;
    }

    return lp + LP_HDR_SIZE;
}

listpackEntry lpLast(listpack lp)
{
    listpackEntry entry = lpFirst(lp);
    if (entry == nullptr)
    {
        return nullptr;
    }

    listpackEntry next = lpNext(lp, entry);
    while (next != nullptr)
    {
        entry = next;
        next = lpNext(lp, entry);
    }

    return entry;
}

listpackEntry lpNext(listpack, listpackEntry entry)
{
    const uint32_t size = entryTotalSize(entry);
    listpackEntry next = entry + size;
    if (next[0] == LP_EOF)
    {
        return nullptr;
    }

    return next;
}

listpackEntry lpPrev(listpack lp, listpackEntry entry)
{
    if (entry <= lp + LP_HDR_SIZE)
    {
        return nullptr;
    }

    uint32_t prevlen = 0;
    readBacklen(entry, &prevlen);
    return entry - prevlen;
}

int lpGet(listpackEntry entry, long long* val, unsigned char** sval, uint32_t* slen)
{
    if (entry == nullptr)
    {
        return -1;
    }

    uint32_t backlen = 0;
    const uint32_t backlen_size = readBacklen(entry, &backlen);
    const unsigned char* enc = entry + backlen_size;
    const unsigned char byte = enc[0];

    if (isUint7(byte))
    {
        if (val != nullptr)
        {
            *val = byte;
        }
        if (sval != nullptr)
        {
            *sval = nullptr;
        }
        if (slen != nullptr)
        {
            *slen = 0;
        }
        return 1;
    }

    if (isString6(byte))
    {
        const uint32_t len = byte & 0x3FU;
        if (val != nullptr)
        {
            *val = 0;
        }
        if (sval != nullptr)
        {
            *sval = const_cast<unsigned char*>(enc + 1);
        }
        if (slen != nullptr)
        {
            *slen = len;
        }
        return 0;
    }

    if (byte == 0xE0U)
    {
        const uint32_t len = readU32Le(enc + 1);
        if (val != nullptr)
        {
            *val = 0;
        }
        if (sval != nullptr)
        {
            *sval = const_cast<unsigned char*>(enc + 5);
        }
        if (slen != nullptr)
        {
            *slen = len;
        }
        return 0;
    }

    if (byte == 0xF0U)
    {
        long long integer = 0;
        std::memcpy(&integer, enc + 1, sizeof(integer));
        if (val != nullptr)
        {
            *val = integer;
        }
        if (sval != nullptr)
        {
            *sval = nullptr;
        }
        if (slen != nullptr)
        {
            *slen = 0;
        }
        return 1;
    }

    return -1;
}

listpack lpAppend(listpack lp, const unsigned char* data, uint32_t len)
{
    const uint32_t encoded_len = (len <= 63) ? (1U + len) : (5U + len);
    const uint32_t prev_entry_size = (lpLength(lp) == 0) ? 0U : entryTotalSize(lpLast(lp));
    const uint32_t backlen_size = (prev_entry_size < LP_BACKLEN_MAX_1BYTE) ? 1U : 5U;
    const uint32_t new_entry_size = backlen_size + encoded_len;
    const uint32_t old_bytes = lpBytes(lp);
    const uint32_t new_bytes = old_bytes + new_entry_size;

    lp = static_cast<listpack>(std::realloc(lp, new_bytes));
    if (lp == nullptr)
    {
        return nullptr;
    }

    const uint32_t insert_at = payloadEndOffset(lp);
    std::memmove(lp + insert_at + new_entry_size, lp + insert_at, 1);

    unsigned char* entry = lp + insert_at;
    const uint32_t written_backlen = encodeBacklen(prev_entry_size, entry);
    const uint32_t written_encoded = encodeString(data, len, entry + written_backlen);
    if (written_backlen + written_encoded != new_entry_size)
    {
        return lp;
    }

    setTotalBytes(lp, new_bytes);
    setNumElements(lp, static_cast<uint16_t>(lpLength(lp) + 1));
    lp[new_bytes - 1] = LP_EOF;
    return lp;
}

listpack lpAppendInteger(listpack lp, long long value)
{
    const uint32_t encoded_len = (value >= 0 && value <= 127) ? 1U : 9U;
    const uint32_t prev_entry_size = (lpLength(lp) == 0) ? 0U : entryTotalSize(lpLast(lp));
    const uint32_t backlen_size = (prev_entry_size < LP_BACKLEN_MAX_1BYTE) ? 1U : 5U;
    const uint32_t new_entry_size = backlen_size + encoded_len;
    const uint32_t old_bytes = lpBytes(lp);
    const uint32_t new_bytes = old_bytes + new_entry_size;

    lp = static_cast<listpack>(std::realloc(lp, new_bytes));
    if (lp == nullptr)
    {
        return nullptr;
    }

    const uint32_t insert_at = payloadEndOffset(lp);
    std::memmove(lp + insert_at + new_entry_size, lp + insert_at, 1);

    unsigned char* entry = lp + insert_at;
    const uint32_t written_backlen = encodeBacklen(prev_entry_size, entry);
    const uint32_t written_encoded = encodeInteger(value, entry + written_backlen);
    if (written_backlen + written_encoded != new_entry_size)
    {
        return lp;
    }

    setTotalBytes(lp, new_bytes);
    setNumElements(lp, static_cast<uint16_t>(lpLength(lp) + 1));
    lp[new_bytes - 1] = LP_EOF;
    return lp;
}
