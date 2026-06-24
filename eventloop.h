#pragma once

#include "client.h"

int runEventLoop(int server_fd);
void clientWritePending(int epoll_fd, Client& client);
