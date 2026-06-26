#include "client.h"

#include <cerrno>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

namespace
{
bool wouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

void compactWrites(Client& client)
{
    if (client.write_chunk_idx < 32)
    {
        return;
    }

    client.pending_writes.erase(
        client.pending_writes.begin(),
        client.pending_writes.begin() + static_cast<std::ptrdiff_t>(client.write_chunk_idx));
    client.write_chunk_idx = 0;
}
}

bool clientHasPendingWrites(const Client& client)
{
    return client.write_chunk_idx < client.pending_writes.size();
}

void clientAppendWrite(Client& client, std::string data)
{
    if (data.empty())
    {
        return;
    }

    client.pending_writes.push_back(std::move(data));
}

void clientAdvanceWrites(Client& client, size_t sent)
{
    while (sent > 0 && client.write_chunk_idx < client.pending_writes.size())
    {
        const std::string& chunk = client.pending_writes[client.write_chunk_idx];
        const size_t remaining = chunk.size() - client.write_chunk_off;
        if (sent >= remaining)
        {
            sent -= remaining;
            ++client.write_chunk_idx;
            client.write_chunk_off = 0;
        }
        else
        {
            client.write_chunk_off += sent;
            sent = 0;
        }
    }

    compactWrites(client);
}

bool clientFlush(Client& client)
{
    while (clientHasPendingWrites(client))
    {
        iovec iov[64];
        int iovcnt = 0;
        size_t idx = client.write_chunk_idx;
        size_t off = client.write_chunk_off;

        while (idx < client.pending_writes.size() && iovcnt < 64)
        {
            const std::string& chunk = client.pending_writes[idx];
            if (off >= chunk.size())
            {
                ++idx;
                off = 0;
                continue;
            }

            iov[iovcnt].iov_base = const_cast<char*>(chunk.data() + off);
            iov[iovcnt].iov_len = chunk.size() - off;
            ++iovcnt;
            off = 0;
            ++idx;
        }

        if (iovcnt == 0)
        {
            client.write_chunk_idx = client.pending_writes.size();
            client.write_chunk_off = 0;
            continue;
        }

        const ssize_t sent = writev(client.fd, iov, iovcnt);
        if (sent > 0)
        {
            clientAdvanceWrites(client, static_cast<size_t>(sent));
            continue;
        }

        if (sent == 0 || (sent < 0 && wouldBlock()))
        {
            return true;
        }

        return false;
    }

    return true;
}
