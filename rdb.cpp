#include "rdb.h"

#include "aof.h"
#include "encoding.h"
#include "object.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace std;

string g_rdb_filename = "dump.rdb";

namespace
{
#if defined(__linux__) || defined(__APPLE__)
pid_t bgsave_child_pid = 0;
bool bgsave_in_progress = false;
#endif
constexpr uint8_t RDB_OPCODE_RESIZEDB = 0xFB;
constexpr uint8_t RDB_OPCODE_EXPIRETIME_MS = 0xFC;
constexpr uint8_t RDB_OPCODE_SELECTDB = 0xFE;
constexpr uint8_t RDB_OPCODE_EOF = 0xFF;

constexpr uint8_t RDB_TYPE_STRING = 0;
constexpr uint8_t RDB_TYPE_LIST = 1;
constexpr uint8_t RDB_TYPE_SET = 2;
constexpr uint8_t RDB_TYPE_ZSET = 3;
constexpr uint8_t RDB_TYPE_HASH = 4;

constexpr uint8_t RDB_ENC_INT8 = 0xC0;
constexpr uint8_t RDB_ENC_INT16 = 0xC1;
constexpr uint8_t RDB_ENC_INT32 = 0xC2;

uint64_t crc64(const uint8_t* data, size_t len)
{
    uint64_t crc = 0;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= static_cast<uint64_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ 0xC96C5795D7870F42ULL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

void flushAll(vector<RedisDb>& databases)
{
    for (RedisDb& db : databases)
    {
        for (auto& item : db.data)
        {
            destroyObject(item.second);
        }

        db.data.clear();
        db.expires.clear();
    }
}

class Writer
{
public:
    void bytes(const void* data, size_t len)
    {
        const auto* raw = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), raw, raw + len);
    }

    void byte(uint8_t value)
    {
        buf_.push_back(value);
    }

    void u32le(uint32_t value)
    {
        byte(static_cast<uint8_t>(value & 0xFF));
        byte(static_cast<uint8_t>((value >> 8) & 0xFF));
        byte(static_cast<uint8_t>((value >> 16) & 0xFF));
        byte(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void u64le(uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
        {
            byte(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
        }
    }

    void dbl(double value)
    {
        uint64_t bits = 0;
        memcpy(&bits, &value, sizeof(value));
        u64le(bits);
    }

    void len(size_t value)
    {
        if (value < 64)
        {
            byte(static_cast<uint8_t>(value << 2));
            return;
        }

        if (value < 16384)
        {
            byte(static_cast<uint8_t>(0x40 | ((value >> 8) & 0x3F)));
            byte(static_cast<uint8_t>(value & 0xFF));
            return;
        }

        byte(0x80);
        u32le(static_cast<uint32_t>(value));
    }

    void lenInt(int64_t value)
    {
        if (value >= 0 && value <= 63)
        {
            byte(static_cast<uint8_t>(RDB_ENC_INT8 | value));
            return;
        }

        if (value >= INT16_MIN && value <= INT16_MAX)
        {
            byte(RDB_ENC_INT16);
            u16le(static_cast<uint16_t>(value));
            return;
        }

        byte(RDB_ENC_INT32);
        u32le(static_cast<uint32_t>(value));
    }

    void stringValue(const string& value)
    {
        long long integer = 0;
        if (tryParseInteger(value, integer))
        {
            lenInt(integer);
            return;
        }

        len(value.size());
        bytes(value.data(), value.size());
    }

    bool writeFile(const string& path) const
    {
        ofstream out(path, ios::binary | ios::trunc);
        if (!out)
        {
            return false;
        }

        out.write(reinterpret_cast<const char*>(buf_.data()), static_cast<streamsize>(buf_.size()));
        return static_cast<bool>(out);
    }

    vector<uint8_t> buf_;

private:
    void u16le(uint16_t value)
    {
        byte(static_cast<uint8_t>(value & 0xFF));
        byte(static_cast<uint8_t>((value >> 8) & 0xFF));
    }
};

class Reader
{
public:
    explicit Reader(vector<uint8_t> data) : data_(std::move(data)) {}

    bool byte(uint8_t& out)
    {
        if (pos_ >= data_.size())
        {
            return false;
        }

        out = data_[pos_++];
        return true;
    }

    bool bytes(void* out, size_t len)
    {
        if (pos_ + len > data_.size())
        {
            return false;
        }

        memcpy(out, data_.data() + pos_, len);
        pos_ += len;
        return true;
    }

    bool u32le(uint32_t& out)
    {
        uint8_t b[4];
        if (!bytes(b, 4))
        {
            return false;
        }

        out = static_cast<uint32_t>(b[0])
            | (static_cast<uint32_t>(b[1]) << 8)
            | (static_cast<uint32_t>(b[2]) << 16)
            | (static_cast<uint32_t>(b[3]) << 24);
        return true;
    }

    bool u64le(uint64_t& out)
    {
        uint8_t b[8];
        if (!bytes(b, 8))
        {
            return false;
        }

        out = 0;
        for (int i = 0; i < 8; ++i)
        {
            out |= static_cast<uint64_t>(b[i]) << (8 * i);
        }

        return true;
    }

    bool dbl(double& out)
    {
        uint64_t bits = 0;
        if (!u64le(bits))
        {
            return false;
        }

        memcpy(&out, &bits, sizeof(out));
        return true;
    }

    bool encodedLen(size_t& out)
    {
        uint8_t first = 0;
        if (!byte(first))
        {
            return false;
        }

        uint8_t kind = first & 0xC0;
        if (kind == 0x00)
        {
            out = first >> 2;
            return true;
        }

        if (kind == 0x40)
        {
            uint8_t second = 0;
            if (!byte(second))
            {
                return false;
            }

            out = ((first & 0x3F) << 8) | second;
            return true;
        }

        if (kind == 0x80)
        {
            uint32_t value = 0;
            if (!u32le(value))
            {
                return false;
            }

            out = value;
            return true;
        }

        return false;
    }

    bool stringValue(string& out)
    {
        uint8_t first = 0;
        if (!byte(first))
        {
            return false;
        }

        if ((first & 0xC0) == RDB_ENC_INT8)
        {
            out = to_string(static_cast<long long>(first & 0x3F));
            return true;
        }

        if (first == RDB_ENC_INT16)
        {
            uint8_t b[2];
            if (!bytes(b, 2))
            {
                return false;
            }

            int16_t value = static_cast<int16_t>(b[0] | (static_cast<uint16_t>(b[1]) << 8));
            out = to_string(static_cast<long long>(value));
            return true;
        }

        if (first == RDB_ENC_INT32)
        {
            uint32_t value = 0;
            if (!u32le(value))
            {
                return false;
            }

            out = to_string(static_cast<long long>(value));
            return true;
        }

        pos_--;
        size_t len = 0;
        if (!encodedLen(len) || pos_ + len > data_.size())
        {
            return false;
        }

        out.assign(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return true;
    }

    bool lenInt(int64_t& out)
    {
        string as_string;
        if (!stringValue(as_string))
        {
            return false;
        }

        try
        {
            out = stoll(as_string);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

private:
    vector<uint8_t> data_;
    size_t pos_ = 0;
};

uint8_t objectRdbType(const RedisObject* obj)
{
    switch (obj->type)
    {
    case OBJ_STRING:
        return RDB_TYPE_STRING;
    case OBJ_LIST:
        return RDB_TYPE_LIST;
    case OBJ_SET:
        return RDB_TYPE_SET;
    case OBJ_ZSET:
        return RDB_TYPE_ZSET;
    case OBJ_HASH:
        return RDB_TYPE_HASH;
    default:
        return RDB_TYPE_STRING;
    }
}

void writeValue(Writer& w, const RedisObject* obj)
{
    switch (obj->type)
    {
    case OBJ_STRING:
        w.stringValue(getStringValue(obj));
        break;
    case OBJ_LIST:
    {
        vector<string> items = listRange(obj, 0, -1);
        w.len(items.size());
        for (const string& item : items)
        {
            w.stringValue(item);
        }
        break;
    }
    case OBJ_SET:
    {
        vector<string> members = setMembers(obj);
        w.len(members.size());
        for (const string& member : members)
        {
            w.stringValue(member);
        }
        break;
    }
    case OBJ_ZSET:
    {
        vector<ZSetEntry> entries = zsetRangeByRank(obj, 0, -1, false);
        w.len(entries.size());
        for (const ZSetEntry& entry : entries)
        {
            w.dbl(entry.score);
            w.stringValue(entry.member);
        }
        break;
    }
    case OBJ_HASH:
    {
        vector<string> flat = hashGetAllFlat(obj);
        w.len(flat.size() / 2);
        for (size_t i = 0; i < flat.size(); i += 2)
        {
            w.stringValue(flat[i]);
            w.stringValue(flat[i + 1]);
        }
        break;
    }
  default:
        w.stringValue(getStringValue(obj));
        break;
    }
}

RedisObject* readValue(Reader& r, uint8_t type)
{
    switch (type)
    {
    case RDB_TYPE_STRING:
    {
        string value;
        if (!r.stringValue(value))
        {
            return nullptr;
        }

        return createStringObject(value);
    }
    case RDB_TYPE_LIST:
    {
        size_t count = 0;
        if (!r.encodedLen(count))
        {
            return nullptr;
        }

        RedisObject* obj = createListObject();
        vector<string> items;
        items.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            string item;
            if (!r.stringValue(item))
            {
                destroyObject(obj);
                return nullptr;
            }

            items.push_back(item);
        }

        listPushBack(obj, items);
        return obj;
    }
    case RDB_TYPE_SET:
    {
        size_t count = 0;
        if (!r.encodedLen(count))
        {
            return nullptr;
        }

        RedisObject* obj = createSetObject();
        vector<string> members;
        members.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            string member;
            if (!r.stringValue(member))
            {
                destroyObject(obj);
                return nullptr;
            }

            members.push_back(member);
        }

        setAdd(obj, members);
        return obj;
    }
    case RDB_TYPE_ZSET:
    {
        size_t count = 0;
        if (!r.encodedLen(count))
        {
            return nullptr;
        }

        RedisObject* obj = createZSetObject();
        for (size_t i = 0; i < count; ++i)
        {
            double score = 0.0;
            string member;
            if (!r.dbl(score) || !r.stringValue(member))
            {
                destroyObject(obj);
                return nullptr;
            }

            long long added = 0;
            long long changed = 0;
            zsetAdd(obj, {{score, member}}, false, false, false, false, false, added, changed);
        }

        return obj;
    }
    case RDB_TYPE_HASH:
    {
        size_t count = 0;
        if (!r.encodedLen(count))
        {
            return nullptr;
        }

        RedisObject* obj = createHashObject();
        for (size_t i = 0; i < count; ++i)
        {
            string field;
            string value;
            bool added = false;
            if (!r.stringValue(field) || !r.stringValue(value))
            {
                destroyObject(obj);
                return nullptr;
            }

            hashSet(obj, field, value, added);
        }

        return obj;
    }
    default:
        return nullptr;
    }
}

string buildSnapshot(const vector<RedisDb>& databases)
{
    Writer w;
    w.bytes("REDIS0011", 9);

    for (size_t db_index = 0; db_index < databases.size(); ++db_index)
    {
        const RedisDb& db = databases[db_index];
        if (db.data.empty())
        {
            continue;
        }

        w.byte(RDB_OPCODE_SELECTDB);
        w.lenInt(static_cast<int64_t>(db_index));
        w.byte(RDB_OPCODE_RESIZEDB);
        w.lenInt(static_cast<int64_t>(db.data.size()));
        w.lenInt(static_cast<int64_t>(db.expires.size()));

        for (const auto& item : db.data)
        {
            auto exp_it = db.expires.find(item.first);
            if (exp_it != db.expires.end())
            {
                w.byte(RDB_OPCODE_EXPIRETIME_MS);
                w.u64le(static_cast<uint64_t>(exp_it->second));
            }

            w.byte(objectRdbType(item.second));
            w.stringValue(item.first);
            writeValue(w, item.second);
        }
    }

    w.byte(RDB_OPCODE_EOF);
    const uint64_t checksum = crc64(w.buf_.data(), w.buf_.size());
    w.u64le(checksum);
    return string(reinterpret_cast<const char*>(w.buf_.data()), w.buf_.size());
}

bool loadSnapshot(const vector<uint8_t>& data, vector<RedisDb>& databases)
{
    if (data.size() < 9 + 8)
    {
        return false;
    }

    if (memcmp(data.data(), "REDIS0011", 9) != 0)
    {
        return false;
    }

    uint64_t expected_crc = 0;
    memcpy(&expected_crc, data.data() + data.size() - 8, 8);
    const uint64_t actual_crc = crc64(data.data(), data.size() - 8);
    if (expected_crc != actual_crc)
    {
        return false;
    }

    Reader reader(vector<uint8_t>(data.begin() + 9, data.end() - 8));
    flushAll(databases);

    size_t current_db = 0;
    long long pending_expire_ms = -1;

    while (true)
    {
        uint8_t opcode = 0;
        if (!reader.byte(opcode))
        {
            return false;
        }

        if (opcode == RDB_OPCODE_EOF)
        {
            return true;
        }

        if (opcode == RDB_OPCODE_SELECTDB)
        {
            int64_t db_index = 0;
            if (!reader.lenInt(db_index) || db_index < 0
                || static_cast<size_t>(db_index) >= databases.size())
            {
                return false;
            }

            current_db = static_cast<size_t>(db_index);
            pending_expire_ms = -1;
            continue;
        }

        if (opcode == RDB_OPCODE_RESIZEDB)
        {
            int64_t ignore_a = 0;
            int64_t ignore_b = 0;
            if (!reader.lenInt(ignore_a) || !reader.lenInt(ignore_b))
            {
                return false;
            }

            continue;
        }

        if (opcode == RDB_OPCODE_EXPIRETIME_MS)
        {
            uint64_t expire_ms = 0;
            if (!reader.u64le(expire_ms))
            {
                return false;
            }

            pending_expire_ms = static_cast<long long>(expire_ms);
            continue;
        }

        string key;
        if (!reader.stringValue(key))
        {
            return false;
        }

        RedisObject* value = readValue(reader, opcode);
        if (value == nullptr)
        {
            return false;
        }

        RedisDb& db = databases[current_db];
        auto existing = db.data.find(key);
        if (existing != db.data.end())
        {
            destroyObject(existing->second);
        }

        db.data[key] = value;
        if (pending_expire_ms >= 0)
        {
            db.expires[key] = pending_expire_ms;
            pending_expire_ms = -1;
        }
        else
        {
            db.expires.erase(key);
        }
    }
}
}

bool saveRDB(const string& path, const vector<RedisDb>& databases)
{
    const string snapshot = buildSnapshot(databases);
    ofstream out(path, ios::binary);
    if (!out)
    {
        return false;
    }

    out.write(snapshot.data(), static_cast<streamsize>(snapshot.size()));
    return static_cast<bool>(out);
}

string serializeRDB(const vector<RedisDb>& databases)
{
    return buildSnapshot(databases);
}

bool loadRDB(const string& path, vector<RedisDb>& databases)
{
    ifstream in(path, ios::binary);
    if (!in)
    {
        return false;
    }

    vector<uint8_t> data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    return loadSnapshot(data, databases);
}

bool loadRDBFromBuffer(const string& data, vector<RedisDb>& databases)
{
    vector<uint8_t> bytes(data.begin(), data.end());
    return loadSnapshot(bytes, databases);
}

bool bgsaveInProgress()
{
#if defined(__linux__) || defined(__APPLE__)
    return bgsave_in_progress;
#else
    return false;
#endif
}

bool startBgsave(const vector<RedisDb>& databases)
{
#if defined(__linux__) || defined(__APPLE__)
    if (bgsave_in_progress || bgrewriteInProgress())
    {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        return false;
    }

    if (pid == 0)
    {
        bool ok = saveRDB(g_rdb_filename, databases);
        _exit(ok ? 0 : 1);
    }

    bgsave_child_pid = pid;
    bgsave_in_progress = true;
    return true;
#else
    (void)databases;
    return false;
#endif
}

void checkBgsaveChild()
{
#if defined(__linux__) || defined(__APPLE__)
    if (!bgsave_in_progress)
    {
        return;
    }

    int status = 0;
    pid_t pid = waitpid(bgsave_child_pid, &status, WNOHANG);
    if (pid == 0)
    {
        return;
    }

    bgsave_in_progress = false;
    bgsave_child_pid = 0;
#else
    return;
#endif
}
