#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <unordered_map>
#include <sstream>
#include <thread>
#include <functional>
#include <mutex>


using namespace std;

void handleClient(
    int client_fd,
    unordered_map<string, string>& db,
    mutex& db_mutex)
{
    while (true)
    {
        char buffer[1024];

        int bytes = recv(
            client_fd,
            buffer,
            sizeof(buffer) - 1,
            0);

        if (bytes <= 0)
        {
            cout << "Client Disconnected\n";
            break;
        }

        buffer[bytes] = '\0';

        string msg(buffer);

        cout << "Received: " << msg << endl;

        stringstream ss(msg);

        string command, key, value;

        ss >> command >> key >> value;

        if (command == "SET")
{
    {
        lock_guard<mutex> lock(db_mutex);
        db[key] = value;
    }

    string response = "OK";

    send(
        client_fd,
        response.c_str(),
        response.size(),
        0);
}
        else if (command == "GET")
{
    string response;

    {
        lock_guard<mutex> lock(db_mutex);

        auto it = db.find(key);

        if (it != db.end())
        {
            response = it->second;
        }
        else
        {
            response = "NOT FOUND";
        }
    }

    send(
        client_fd,
        response.c_str(),
        response.size(),
        0);
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

    sockaddr_in server_addr;          // A structure that stores the socket's address information (IP, port,Network type)?
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY; // Accept connections on this machine

    // Take this phone (server_fd) and assign it Port 8080
    if (bind(server_fd,
             (sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        cout << "Bind Failed\n";
        return 1;
    }
    cout << "Bind Successful\n";

    // Listen for connections
    if (listen(server_fd, 5) < 0) // Maximum 5 connection requests can wait in queue.
    {
        cout << "Listen Failed\n";
        return 1;
    }
    cout << "Listening...\n";

    // Another socket for particular client connection and is filled with client information after connection. This is the socket that will be used to communicate with the client.
    sockaddr_in client_addr;
    socklen_t client_size = sizeof(client_addr);
    
    mutex db_mutex;

    unordered_map<string,string> db;
    while (true)
{
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
    client_fd,
    ref(db),
    ref(db_mutex));
    
    t.detach();
}



    return 0;
}