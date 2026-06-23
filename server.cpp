#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <cerrno>
#include <cstring>

#include "parser.h"
#include "resp.h"

using namespace std;

namespace
{
constexpr int PORT = 8080;
constexpr size_t BUFFER_SIZE = 1024;
constexpr size_t MAX_REQUEST_BUFFER_SIZE = 4096;

unordered_map<string, string> db;
mutex db_mutex;

bool sendAll(int client_fd, const string& response)
{
    size_t total_sent = 0;

    while (total_sent < response.size())
    {
        ssize_t sent = send(
            client_fd,
            response.data() + total_sent,
            response.size() - total_sent,
            0);

        if (sent <= 0)
        {
            return false;
        }

        total_sent += static_cast<size_t>(sent);
    }

    return true;
}

string processCommand(const vector<string>& argv)
{
    return dispatch(argv, db, db_mutex);
}
}

void handleClient(
    int client_fd)
{
    RespParser parser;

    while (true)
    {
        char buffer[BUFFER_SIZE];

        ssize_t bytes = recv(
            client_fd,
            buffer,
            sizeof(buffer),
            0);

        if (bytes <= 0)
        {
            cout << "Client Disconnected\n";
            break;
        }

        parser.feed(buffer, static_cast<size_t>(bytes));

        if (parser.bufferedSize() > MAX_REQUEST_BUFFER_SIZE)
        {
            sendAll(client_fd, encodeError("ERR request too large"));
            break;
        }

        vector<string> argv;
        try
        {
            while (parser.tryParse(argv))
            {
                cout << "Received command with " << argv.size() << " argument(s)" << endl;

                string response = processCommand(argv);

                if (!sendAll(client_fd, response))
                {
                    close(client_fd);
                    return;
                }
            }
        }
        catch (const invalid_argument& err)
        {
            string response = encodeError(string("ERR ") + err.what());
            if (!sendAll(client_fd, response))
            {
                close(client_fd);
                return;
            }
            break;
        }
    }

    close(client_fd);
}


int main()
{

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
    while (true)
    {
        // Another socket for particular client connection and is filled with client information after connection. This is the socket that will be used to communicate with the client.
        sockaddr_in client_addr;
        socklen_t client_size = sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (sockaddr *)&client_addr,
            &client_size);

        if (client_fd < 0)
        {
            cout << "Accept Failed\n";
            continue;
        }

        cout << "Client Connected!\n";

        thread t(
            handleClient,
            client_fd);

        t.detach();
    }



    return 0;
}
