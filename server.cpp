#include "eventloop.h"
#include "eviction.h"
#include "aof.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

namespace {
constexpr int DEFAULT_PORT = 8080;
int g_server_fd_global = -1;
}

static void handleSignal(int /*sig*/) {
    g_shutdown_requested = 1;
}

static void printBanner(int port) {
    cout << "\n"
         << "  ____          _ _       _     _ _       \n"
         << " |  _ \\ ___  __| (_)___  | |   (_) |_ ___ \n"
         << " | |_) / _ \\/ _` | / __| | |   | | __/ _ \\\n"
         << " |  _ <  __/ (_| | \\__ \\ | |___| | ||  __/\n"
         << " |_| \\_\\___|\\__,_|_|___/ |_____|_|\\__\\___|\n"
         << "\n"
         << "  Redis-compatible in-memory store  |  C++17  |  epoll reactor\n"
         << "  Port    : " << port << "\n"
         << "  AOF     : " << (g_aof_enabled ? g_aof_filename : "disabled") << "\n"
         << "\n";
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, handleSignal);
    signal(SIGINT,  handleSignal);

    if (!parseServerArgs(argc, argv)) return 1;

    // Env var overrides — Docker-friendly; CLI flags take precedence if both set
    if (const char* v = getenv("AOF_PATH"))   { g_aof_filename = v; }
    if (const char* v = getenv("APPENDONLY")) {
        string val(v);
        if      (val == "yes" || val == "YES") g_aof_enabled = true;
        else if (val == "no"  || val == "NO")  g_aof_enabled = false;
    }
    if (const char* v = getenv("PORT")) {
        try { if (g_server_config.port <= 0) g_server_config.port = stoi(v); }
        catch (...) {}
    }

    const int port = g_server_config.port > 0 ? g_server_config.port : DEFAULT_PORT;

    printBanner(port);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { cerr << "Socket creation failed\n"; return 1; }
    g_server_fd_global = server_fd;

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        cerr << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << "\n";
        close(server_fd); return 1;
    }
    int flag = 1;
    if (setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        cerr << "setsockopt(TCP_NODELAY) failed: " << strerror(errno) << "\n";
        close(server_fd); return 1;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        cerr << "Bind failed: " << strerror(errno) << "\n";
        close(server_fd); return 1;
    }
    if (listen(server_fd, 4096) < 0) {
        cerr << "Listen failed: " << strerror(errno) << "\n";
        close(server_fd); return 1;
    }
    cout << "Listening on :" << port << "\n";

    const int result = runEventLoop(server_fd);
    close(server_fd);
    return result;
}
