#pragma once

#include "client.h"

#include <csignal>

extern volatile sig_atomic_t g_shutdown_requested;

int runEventLoop(int server_fd);
void clientWritePending(int epoll_fd, Client& client);
