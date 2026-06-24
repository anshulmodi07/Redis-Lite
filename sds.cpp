#include "sds.h"

#include <cstdlib>
#include <cstring>

namespace
{
constexpr size_t SDS_INIT_SIZE = 16;

struct SdsHeader
{
    size_t len;
    size_t alloc;
};

SdsHeader* sdsHdr(const sds s)
{
    return reinterpret_cast<SdsHeader*>(s - sizeof(SdsHeader));
}

sds sdsFromHdr(SdsHeader* hdr)
{
    return reinterpret_cast<char*>(hdr) + sizeof(SdsHeader);
}

size_t nextCapacity(size_t required)
{
    size_t capacity = SDS_INIT_SIZE;
    while (capacity < required)
    {
        capacity *= 2;
    }
    return capacity;
}
}

sds sdsnewlen(const void* init, size_t initlen)
{
    const size_t capacity = nextCapacity(initlen);
    auto* hdr = static_cast<SdsHeader*>(std::malloc(sizeof(SdsHeader) + capacity + 1));
    if (hdr == nullptr)
    {
        return nullptr;
    }

    hdr->len = initlen;
    hdr->alloc = capacity;

    sds s = sdsFromHdr(hdr);
    if (initlen > 0 && init != nullptr)
    {
        std::memcpy(s, init, initlen);
    }
    s[initlen] = '\0';
    return s;
}

sds sdsnew(const char* init)
{
    if (init == nullptr)
    {
        return sdsnewlen("", 0);
    }

    return sdsnewlen(init, std::strlen(init));
}

void sdsfree(sds s)
{
    if (s == nullptr)
    {
        return;
    }

    std::free(sdsHdr(s));
}

size_t sdslen(const sds s)
{
    if (s == nullptr)
    {
        return 0;
    }

    return sdsHdr(s)->len;
}

sds sdsgrow(sds s, size_t addlen)
{
    SdsHeader* hdr = sdsHdr(s);
    const size_t required = hdr->len + addlen;
    if (required <= hdr->alloc)
    {
        return s;
    }

    const size_t new_alloc = nextCapacity(required);
    hdr = static_cast<SdsHeader*>(std::realloc(hdr, sizeof(SdsHeader) + new_alloc + 1));
    if (hdr == nullptr)
    {
        return nullptr;
    }

    hdr->alloc = new_alloc;
    return sdsFromHdr(hdr);
}

sds sdscatlen(sds s, const void* t, size_t len)
{
    if (len == 0)
    {
        return s;
    }

    s = sdsgrow(s, len);
    if (s == nullptr)
    {
        return nullptr;
    }

    SdsHeader* hdr = sdsHdr(s);
    std::memcpy(s + hdr->len, t, len);
    hdr->len += len;
    s[hdr->len] = '\0';
    return s;
}

sds sdscat(sds s, const char* t)
{
    if (t == nullptr)
    {
        return s;
    }

    return sdscatlen(s, t, std::strlen(t));
}
