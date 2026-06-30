#include "eventloop.h"

#include "aof.h"
#include "client.h"
#include "cluster.h"
#include "commands.h"
#include "db.h"
#include "eviction.h"
#include "multi.h"
#include "parser.h"
#include "pubsub.h"
#include "rdb.h"
#include "replication.h"
#include "resp.h"
#include "scripting.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

long long g_cached_time_ms = 0;
void (*g_client_write_pending_cb)(int epoll_fd, Client &client) = nullptr;

namespace {
constexpr size_t MAX_REQUEST_BUFFER_SIZE = 16 * 1024 * 1024;
constexpr int MAX_EVENTS = 64;
constexpr int EPOLL_WAIT_TIMEOUT_MS = 100;
constexpr size_t DB_COUNT = 16;

vector<RedisDb> databases(DB_COUNT);

int computeNextTimerMs() {
  long long now = g_cached_time_ms > 0 ? g_cached_time_ms : nowMs();
  long long earliest = INT64_MAX;

  for (const auto &db : databases) {
    for (const auto &pair : db.expires) {
      long long remaining = pair.second - now;
      if (remaining < earliest) {
        earliest = remaining;
      }
    }
  }

  if (earliest == INT64_MAX) {
    return 100;
  }
  if (earliest <= 0)
    return 0;
  return static_cast<int>(std::min(earliest, 100LL));
}

void initDatabases() {
  for (auto &db : databases) {
    db.data.reserve(100000);
    db.expires.reserve(100000);
  }
}

void initDatabasesOnce() {
  static bool initialized = false;
  if (!initialized) {
    initDatabases();
    initialized = true;
  }
}

// Fair write scheduling structures
Client *fast_clients[65536] = {nullptr};
std::deque<int> write_ready;
std::unordered_set<int> in_write_ready;

void enqueueWrite(int fd) {
  if (in_write_ready.insert(fd).second) {
    write_ready.push_back(fd);
  }
}

void dequeueWrite(int fd) {
  in_write_ready.erase(fd);
  for (auto it = write_ready.begin(); it != write_ready.end(); ++it) {
    if (*it == fd) {
      write_ready.erase(it);
      break;
    }
  }
}

bool setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }

  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool addToEpoll(int epoll_fd, int fd, uint32_t events) {
  epoll_event event;
  event.events = events;
  event.data.fd = fd;
  return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == 0;
}

bool updateClientEvents(int epoll_fd, const Client &client) {
  epoll_event event;
  event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
  if (!client.write_buf.empty()) {
    event.events |= EPOLLOUT;
  }

  event.data.fd = client.fd;
  return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client.fd, &event) == 0;
}

void closeClient(int epoll_fd, unordered_map<int, Client> &clients, int fd) {
  pubsubCleanup(fd);
  watchCleanup(fd, clients);
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
  clients.erase(fd);
  dequeueWrite(fd);
  if (fd >= 0 && fd < 65536) {
    fast_clients[fd] = nullptr;
  }
}

bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }

void acceptReadyClients(int epoll_fd, int server_fd,
                        unordered_map<int, Client> &clients) {
  while (true) {
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
      if (wouldBlock()) {
        return;
      }

      cout << "Accept failed: " << strerror(errno) << "\n";
      return;
    }

    if (!setNonBlocking(client_fd)) {
      cout << "Failed to set client non-blocking: " << strerror(errno) << "\n";
      close(client_fd);
      continue;
    }

    int flag = 1;
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) <
        0) {
      cout << "setsockopt(TCP_NODELAY) failed on client: " << strerror(errno)
           << "\n";
    }

    int sndbuf = 131072; // 128 KB
    if (setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) <
        0) {
      cout << "setsockopt(SO_SNDBUF) failed: " << strerror(errno) << "\n";
    }
    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &sndbuf, sizeof(sndbuf)) <
        0) {
      cout << "setsockopt(SO_RCVBUF) failed: " << strerror(errno) << "\n";
    }

    Client client;
    client.fd = client_fd;
    client.parsed_argv_cache.reserve(32);
    if (!addToEpoll(epoll_fd, client_fd, EPOLLIN | EPOLLERR | EPOLLHUP)) {
      cout << "Failed to register client with epoll: " << strerror(errno)
           << "\n";
      close(client_fd);
      continue;
    }

    auto emplace_result = clients.emplace(client_fd, std::move(client));
    if (client_fd >= 0 && client_fd < 65536) {
      fast_clients[client_fd] = &emplace_result.first->second;
    }
    cout << "Client connected\n";
    ++g_stats.total_connections_received;
  }
}

void queueParsedReplies(Client &client, unordered_map<int, Client> &clients,
                        int epoll_fd) {
  while (client.parser.tryParse(client.parsed_argv_cache)) {
    client.write_buf += dispatch(client, databases, client.parsed_argv_cache,
                                 &clients, epoll_fd);
  }
}

bool readClient(Client &client, unordered_map<int, Client> &clients,
                int epoll_fd) {
  char buffer[65536];
  ssize_t bytes = recv(client.fd, buffer, sizeof(buffer), 0);
  if (bytes > 0) {
    client.parser.feed(buffer, static_cast<size_t>(bytes));

    if (client.parser.bufferedSize() > MAX_REQUEST_BUFFER_SIZE) {
      client.write_buf += encodeError("ERR request too large");
      client.closing = true;
      enqueueWrite(client.fd);
      return true;
    }

    try {
      queueParsedReplies(client, clients, epoll_fd);
      aofFlush();
    } catch (const invalid_argument &err) {
      client.write_buf += encodeError(string("ERR ") + err.what());
      client.closing = true;
      enqueueWrite(client.fd);
      return true;
    }

    if (!client.write_buf.empty()) {
      enqueueWrite(client.fd);
    }

    if (client.closing && client.write_buf.empty()) {
      return false;
    }

    return true;
  }

  if (bytes == 0) {
    client.closing = true;
    if (!client.write_buf.empty()) {
      enqueueWrite(client.fd);
    }
    return !client.write_buf.empty();
  }

  if (wouldBlock()) {
    return true;
  }

  return false;
}
} // namespace

void clientWritePending(int epoll_fd, Client &client) {
  if (!client.write_buf.empty()) {
    ssize_t sent =
        send(client.fd, client.write_buf.data(), client.write_buf.size(), 0);
    if (sent > 0) {
      client.write_buf.consume(sent);
    }
  }

  if (!client.write_buf.empty()) {
    enqueueWrite(client.fd);
  }

  epoll_event event;
  event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
  if (!client.write_buf.empty()) {
    event.events |= EPOLLOUT;
  }

  event.data.fd = client.fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client.fd, &event);
}

int runEventLoop(int server_fd) {
  g_client_write_pending_cb = clientWritePending;
  initDatabasesOnce();
  Resp::init();
  initCommandTable();
  initScripting();
  initReplication();
  initCluster();
  aofInit();

  if (g_aof_enabled && ifstream(g_aof_filename).good()) {
    if (!aofLoad(databases)) {
      cout << "Failed to load AOF file: " << g_aof_filename << "\n";
    }
  } else if (ifstream(g_rdb_filename).good() &&
             !loadRDB(g_rdb_filename, databases)) {
    cout << "Failed to load RDB file: " << g_rdb_filename << "\n";
  }

  if (!setNonBlocking(server_fd)) {
    cout << "Failed to set server non-blocking: " << strerror(errno) << "\n";
    return 1;
  }

  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    cout << "epoll_create1 failed: " << strerror(errno) << "\n";
    return 1;
  }

  if (!addToEpoll(epoll_fd, server_fd, EPOLLIN | EPOLLERR | EPOLLHUP)) {
    cout << "Failed to register server with epoll: " << strerror(errno) << "\n";
    close(epoll_fd);
    return 1;
  }

  unordered_map<int, Client> clients;
  vector<epoll_event> events(MAX_EVENTS);

  replicationSetContext(&databases, &clients, epoll_fd);
  if (g_server_config.readonly_replica) {
    replicationStartReplica(epoll_fd);
  }
  g_stats.start_time_ms = nowMs();
  g_stats.last_sample_time_ms = g_stats.start_time_ms;
  g_stats.last_sample_commands = 0;
  long long last_cron_ms = 0;
  constexpr int CRON_INTERVAL_MS = 100;

  while (true) {
    g_cached_time_ms = nowMs();
    long long current_time = g_cached_time_ms;
    if (current_time - g_stats.last_sample_time_ms >= 1000) {
      long long elapsed_ms = current_time - g_stats.last_sample_time_ms;
      long long commands =
          g_stats.total_commands_processed - g_stats.last_sample_commands;
      if (elapsed_ms > 0) {
        g_stats.ops_per_sec = (commands * 1000) / elapsed_ms;
      }
      g_stats.last_sample_time_ms = current_time;
      g_stats.last_sample_commands = g_stats.total_commands_processed;
    }
    int timeout_ms = computeNextTimerMs();
    int ready = epoll_wait(epoll_fd, events.data(),
                           static_cast<int>(events.size()), timeout_ms);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }

      cout << "epoll_wait failed: " << strerror(errno) << "\n";
      close(epoll_fd);
      return 1;
    }

    g_cached_time_ms = nowMs();
    if (g_cached_time_ms - last_cron_ms >= CRON_INTERVAL_MS) {
      for (RedisDb &db : databases) {
        activeExpireCycle(db);
      }
      last_cron_ms = g_cached_time_ms;
    }

    checkBgsaveChild();
    checkBgrewriteChild();
    aofPeriodic(timeout_ms > 0 ? timeout_ms : 1);
    replicationPeriodic(epoll_fd);
    clusterPeriodic();

    vector<int> to_close;
    for (int i = 0; i < ready; ++i) {
      int fd = events[static_cast<size_t>(i)].data.fd;
      uint32_t fired = events[static_cast<size_t>(i)].events;
      if (replicationIsMasterLink(fd)) {
        if (fired & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
          replicationHandleMasterEvent(epoll_fd);
        }
        continue;
      }

      if (fd == server_fd) {
        if (fired & EPOLLIN) {
          acceptReadyClients(epoll_fd, server_fd, clients);
        }
        continue;
      }

      Client *client_ptr = (fd >= 0 && fd < 65536) ? fast_clients[fd] : nullptr;
      if (client_ptr == nullptr) {
        continue;
      }

      Client &client = *client_ptr;
      bool keep_open = true;

      if (fired & EPOLLERR) {
        keep_open = false;
      }

      if (keep_open && (fired & EPOLLIN)) {
        keep_open = readClient(client, clients, epoll_fd);
      }

      if (keep_open && (fired & EPOLLOUT)) {
        enqueueWrite(fd);
      }

      if (keep_open && (fired & EPOLLHUP) && client.write_buf.empty()) {
        keep_open = false;
      }

      if (keep_open && client.closing && client.write_buf.empty()) {
        keep_open = false;
      }

      if (!keep_open) {
        to_close.push_back(fd);
      } else if (!updateClientEvents(epoll_fd, client)) {
        to_close.push_back(fd);
      }
    }

    // --- Round-robin write drain ---
    constexpr size_t WRITE_BUDGET_BYTES = 131072; // 128 KB per client per tick
    size_t clients_to_drain = write_ready.size();
    while (clients_to_drain-- > 0 && !write_ready.empty()) {
      int fd = write_ready.front();
      write_ready.pop_front();
      in_write_ready.erase(fd);

      Client *client_ptr = (fd >= 0 && fd < 65536) ? fast_clients[fd] : nullptr;
      if (client_ptr == nullptr) {
        continue;
      }

      Client &client = *client_ptr;
      if (client.write_buf.empty()) {
        if (client.closing) {
          to_close.push_back(fd);
        } else {
          updateClientEvents(epoll_fd, client);
        }
        continue;
      }

      size_t to_write = std::min(client.write_buf.size(), WRITE_BUDGET_BYTES);
      ssize_t n = send(fd, client.write_buf.data(), to_write, 0);
      bool re_enqueue = false;
      bool keep_open = true;

      if (n > 0) {
        client.write_buf.consume(n);
        if (!client.write_buf.empty() && static_cast<size_t>(n) == to_write) {
          re_enqueue = true;
        }
      } else if (n < 0 && wouldBlock()) {
        re_enqueue = false;
      } else {
        keep_open = false;
      }

      if (!keep_open || (client.closing && client.write_buf.empty())) {
        to_close.push_back(fd);
      } else {
        if (re_enqueue) {
          enqueueWrite(fd);
        }
        updateClientEvents(epoll_fd, client);
      }
    }

    for (int fd : to_close) {
      closeClient(epoll_fd, clients, fd);
      cout << "Client disconnected\n";
    }
  }
}