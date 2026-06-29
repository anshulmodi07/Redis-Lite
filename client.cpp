#include "client.h"

#include <cerrno>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

void (*g_client_write_pending_cb)(int epoll_fd, Client& client) = nullptr;
