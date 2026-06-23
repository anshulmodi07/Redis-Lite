#include "eventloop.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

namespace
{
constexpr int PORT = 8080;
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
    cout << "Server Starting...\n";
    int server_fd = socket(AF_INET, SOCK_STREAM, 0); // Create a TCP socket
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

    sockaddr_in server_addr;          // A structure that stores the socket's address information (IP, port,Network type)?
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY; // Accept connections on this machine

    // Take this phone (server_fd) and assign it Port 8080
    if (bind(server_fd,
             (sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        cout << "Bind Failed\n";
        close(server_fd);
        return 1;
    }
    cout << "Bind Successful\n";

    // Listen for connections
    if (listen(server_fd, 5) < 0) // Maximum 5 connection requests can wait in queue.
    {
        cout << "Listen Failed\n";
        close(server_fd);
        return 1;
    }
    cout << "Listening...\n";
    int result = runEventLoop(server_fd);
    close(server_fd);
    return result;
}
