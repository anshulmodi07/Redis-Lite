#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

using namespace std;

int main()
{
    cout << "Client Starting...\n";

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (client_fd < 0)
    {
        cout << "Socket Creation Failed\n";
        return 1;
    }

    cout << "Socket Created\n";

    sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;   // Use IPv4
    server_addr.sin_port = htons(8080); // Server is listening on port 8080

    inet_pton(
        AF_INET,
        "127.0.0.1",
        &server_addr.sin_addr);

    if (connect(
            client_fd,
            (sockaddr *)&server_addr,
            sizeof(server_addr)) < 0)
    {
        cout << "Connection Failed\n";
        return 1;
    }

    cout << "Connected To Server!\n";
    string msg;

    getline(cin, msg);
    msg.push_back('\n');

    send(
        client_fd,
        msg.c_str(),
        msg.size(),
        0);
    char buffer[1024];

    int bytes = recv(
        client_fd,
        buffer,
        sizeof(buffer),
        0);
    if (bytes <= 0)
    {
        cout << "Server disconnected\n";
        return 1;
    }
    buffer[bytes] = '\0';
    cout << buffer << endl;
    return 0;
}
