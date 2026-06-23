#include "eventloop.h"

#include "client.h"
#include "parser.h"
#include "resp.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

namespace
{
constexpr size_t BUFFER_SIZE = 1024;
constexpr size_t MAX_REQUEST_BUFFER_SIZE = 4096;

unordered_map<string, string> db;

bool setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void closeClient(unordered_map<int, Client>& clients, int fd)
{
    close(fd);
    clients.erase(fd);
}

bool wouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

void acceptReadyClients(int server_fd, unordered_map<int, Client>& clients)
{
    while (true)
    {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0)
        {
            if (wouldBlock())
            {
                return;
            }

            cout << "Accept failed: " << strerror(errno) << "\n";
            return;
        }

        if (!setNonBlocking(client_fd))
        {
            cout << "Failed to set client non-blocking: " << strerror(errno) << "\n";
            close(client_fd);
            continue;
        }

        Client client;
        client.fd = client_fd;
        clients.emplace(client_fd, std::move(client));
        cout << "Client connected\n";
    }
}

void queueParsedReplies(Client& client)
{
    vector<string> argv;

    while (client.parser.tryParse(argv))
    {
        client.write_buf += dispatch(argv, db);
    }
}

bool readClient(Client& client)
{
    char buffer[BUFFER_SIZE];

    while (true)
    {
        ssize_t bytes = recv(client.fd, buffer, sizeof(buffer), 0);
        if (bytes > 0)
        {
            client.parser.feed(buffer, static_cast<size_t>(bytes));

            if (client.parser.bufferedSize() > MAX_REQUEST_BUFFER_SIZE)
            {
                client.write_buf += encodeError("ERR request too large");
                client.closing = true;
                return true;
            }

            try
            {
                queueParsedReplies(client);
            }
            catch (const invalid_argument& err)
            {
                client.write_buf += encodeError(string("ERR ") + err.what());
                client.closing = true;
                return true;
            }

            continue;
        }

        if (bytes == 0)
        {
            client.closing = true;
            return !client.write_buf.empty();
        }

        if (wouldBlock())
        {
            return true;
        }

        return false;
    }
}

bool flushClient(Client& client)
{
    while (!client.write_buf.empty())
    {
        ssize_t sent = send(
            client.fd,
            client.write_buf.data(),
            client.write_buf.size(),
            0);

        if (sent > 0)
        {
            client.write_buf.erase(0, static_cast<size_t>(sent));
            continue;
        }

        if (sent < 0 && wouldBlock())
        {
            return true;
        }

        return false;
    }

    return !client.closing;
}
}

int runEventLoop(int server_fd)
{
    if (!setNonBlocking(server_fd))
    {
        cout << "Failed to set server non-blocking: " << strerror(errno) << "\n";
        return 1;
    }

    unordered_map<int, Client> clients;

    while (true)
    {
        vector<pollfd> fds;
        fds.reserve(clients.size() + 1);
        fds.push_back({server_fd, POLLIN, 0});

        for (const auto& entry : clients)
        {
            short events = POLLIN;
            if (!entry.second.write_buf.empty())
            {
                events |= POLLOUT;
            }

            fds.push_back({entry.first, events, 0});
        }

        int ready = poll(fds.data(), fds.size(), -1);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            cout << "poll failed: " << strerror(errno) << "\n";
            return 1;
        }

        if (fds[0].revents & POLLIN)
        {
            acceptReadyClients(server_fd, clients);
        }

        vector<int> to_close;
        for (size_t i = 1; i < fds.size(); ++i)
        {
            int fd = fds[i].fd;
            auto it = clients.find(fd);
            if (it == clients.end())
            {
                continue;
            }

            Client& client = it->second;
            bool keep_open = true;

            if (fds[i].revents & (POLLERR | POLLNVAL))
            {
                keep_open = false;
            }

            if (keep_open && (fds[i].revents & POLLIN))
            {
                keep_open = readClient(client);
            }

            if (keep_open && (fds[i].revents & POLLOUT))
            {
                keep_open = flushClient(client);
            }

            if (keep_open && (fds[i].revents & POLLHUP) && client.write_buf.empty())
            {
                keep_open = false;
            }

            if (keep_open && client.closing && client.write_buf.empty())
            {
                keep_open = false;
            }

            if (!keep_open)
            {
                to_close.push_back(fd);
            }
        }

        for (int fd : to_close)
        {
            closeClient(clients, fd);
            cout << "Client disconnected\n";
        }
    }
}
