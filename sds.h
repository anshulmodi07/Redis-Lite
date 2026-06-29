#pragma once

#include <cstddef>

// SDS pointer points at the null-terminated payload; header lives immediately before it.
using sds = char*;

sds sdsnew(const char* init);
sds sdsnewlen(const void* init, size_t initlen);
void sdsfree(sds s);
size_t sdslen(const sds s);
sds sdsgrow(sds s, size_t addlen);
sds sdscat(sds s, const char* t);
sds sdscatlen(sds s, const void* t, size_t len);
void sdssetlen(sds s, size_t len);
void sdsclear(sds s);
