#include "eventloop.h"

#include "eviction.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

namespace
{
constexpr int DEFAULT_PORT = 8080;
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, NULL, _IONBF, 0);
    std::setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGPIPE, SIG_IGN);
    if (!parseServerArgs(argc, argv))
    {
        return 1;
    }

    const int port = g_server_config.port > 0 ? g_server_config.port : DEFAULT_PORT;
    cout << "Server Starting on port " << port << "...\n";
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        cout << "Socket creation failed\n";
        return 1;
    }

    int opt = 1;
    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)) < 0)
    {
        cout << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    int flag = 1;
    if (setsockopt(
            server_fd,
            IPPROTO_TCP,
            TCP_NODELAY,
            &flag,
            sizeof(flag)) < 0)
    {
        cout << "setsockopt(TCP_NODELAY) failed: " << strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(port));
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0)
    {
        cout << "Bind Failed\n";
        close(server_fd);
        return 1;
    }
    cout << "Bind Successful\n";

    if (listen(server_fd, 4096) < 0)
    {
        cout << "Listen Failed\n";
        close(server_fd);
        return 1;
    }
    cout << "Listening...\n";
    const int result = runEventLoop(server_fd);
    close(server_fd);
    return result;
}
