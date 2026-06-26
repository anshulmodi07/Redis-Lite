#pragma once

#include "resp.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

struct WriteBuffer
{
    std::vector<char> buf;
    size_t head = 0;

    WriteBuffer()
    {
        buf.reserve(65536);
    }

    void append(const char* data, size_t len)
    {
        buf.insert(buf.end(), data, data + len);
    }

    void append_str(const std::string& s)
    {
        buf.insert(buf.end(), s.data(), s.data() + s.size());
    }

    WriteBuffer& operator+=(const std::string& s)
    {
        append(s.data(), s.size());
        return *this;
    }

    const char* data() const
    {
        return buf.data() + head;
    }

    size_t size() const
    {
        return buf.size() - head;
    }

    size_t readable() const
    {
        return buf.size() - head;
    }

    bool empty() const
    {
        return head == buf.size();
    }

    void consume(size_t n)
    {
        head += n;
        if (head == buf.size())
        {
            clear();
        }
        else if (head > 65536 && head > buf.size() / 2)
        {
            std::copy(buf.begin() + head, buf.end(), buf.begin());
            buf.resize(buf.size() - head);
            head = 0;
        }
    }

    void erase(size_t pos, size_t n)
    {
        if (pos == 0)
        {
            consume(n);
        }
        else
        {
            buf.erase(buf.begin() + head + pos, buf.begin() + head + pos + n);
        }
    }

    void clear()
    {
        buf.clear();
        head = 0;
    }
};

struct Client
{
    int fd = -1;
    int db_index = 0;
    bool pubsub_mode = false;
    bool in_multi = false;
    bool multi_error = false;
    bool dirty = false;
    std::unordered_set<std::string> watches;
    std::vector<std::vector<std::string>> queued_commands;
    RespParser parser;
    WriteBuffer write_buf;
    bool closing = false;
};

inline void (*g_client_write_pending_cb)(int epoll_fd, Client& client) = nullptr;

