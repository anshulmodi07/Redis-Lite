#include "eventloop.h"

#include "client.h"
#include "parser.h"
#include "resp.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/epoll.h>
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
constexpr int MAX_EVENTS = 64;

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

bool addToEpoll(int epoll_fd, int fd, uint32_t events)
{
    epoll_event event;
    event.events = events;
    event.data.fd = fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == 0;
}

bool updateClientEvents(int epoll_fd, const Client& client)
{
    epoll_event event;
    event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    if (!client.write_buf.empty())
    {
        event.events |= EPOLLOUT;
    }

    event.data.fd = client.fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client.fd, &event) == 0;
}

void closeClient(int epoll_fd, unordered_map<int, Client>& clients, int fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    clients.erase(fd);
}

bool wouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

void acceptReadyClients(
    int epoll_fd,
    int server_fd,
    unordered_map<int, Client>& clients)
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
        if (!addToEpoll(epoll_fd, client_fd, EPOLLIN | EPOLLERR | EPOLLHUP))
        {
            cout << "Failed to register client with epoll: " << strerror(errno) << "\n";
            close(client_fd);
            continue;
        }

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

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        cout << "epoll_create1 failed: " << strerror(errno) << "\n";
        return 1;
    }

    if (!addToEpoll(epoll_fd, server_fd, EPOLLIN | EPOLLERR | EPOLLHUP))
    {
        cout << "Failed to register server with epoll: " << strerror(errno) << "\n";
        close(epoll_fd);
        return 1;
    }

    unordered_map<int, Client> clients;
    vector<epoll_event> events(MAX_EVENTS);

    while (true)
    {
        int ready = epoll_wait(
            epoll_fd,
            events.data(),
            static_cast<int>(events.size()),
            -1);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            cout << "epoll_wait failed: " << strerror(errno) << "\n";
            close(epoll_fd);
            return 1;
        }

        vector<int> to_close;
        for (int i = 0; i < ready; ++i)
        {
            int fd = events[static_cast<size_t>(i)].data.fd;
            uint32_t fired = events[static_cast<size_t>(i)].events;

            if (fd == server_fd)
            {
                if (fired & EPOLLIN)
                {
                    acceptReadyClients(epoll_fd, server_fd, clients);
                }
                continue;
            }

            auto it = clients.find(fd);
            if (it == clients.end())
            {
                continue;
            }

            Client& client = it->second;
            bool keep_open = true;

            if (fired & EPOLLERR)
            {
                keep_open = false;
            }

            if (keep_open && (fired & EPOLLIN))
            {
                keep_open = readClient(client);
            }

            if (keep_open && (fired & EPOLLOUT))
            {
                keep_open = flushClient(client);
            }

            if (keep_open && (fired & EPOLLHUP) && client.write_buf.empty())
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
            else if (!updateClientEvents(epoll_fd, client))
            {
                to_close.push_back(fd);
            }
        }

        for (int fd : to_close)
        {
            closeClient(epoll_fd, clients, fd);
            cout << "Client disconnected\n";
        }
    }
}
