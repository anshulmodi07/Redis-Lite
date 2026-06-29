#include "replication.h"

#include "aof.h"
#include "client.h"
#include "commands.h"
#include "eventloop.h"
#include "eviction.h"
#include "parser.h"
#include "rdb.h"
#include "resp.h"


#include <iostream>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <random>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

namespace {
constexpr size_t REPL_BACKLOG_SIZE = 1024 * 1024;

string g_repl_id;
long long g_master_repl_offset = 0;
string repl_backlog;
size_t repl_backlog_start = 0;

vector<RedisDb> *g_databases = nullptr;
unordered_map<int, Client> *g_clients = nullptr;

int master_link_fd = -1;
bool master_link_registered = false;
bool full_sync_mode = false;
size_t full_sync_expected = 0;
string master_read_buffer;
RespParser master_parser;
int g_epoll_fd = -1;

struct ReplicaConn {
  long long repl_offset = 0;
};

unordered_map<int, ReplicaConn> replicas;

string randomReplId() {
  static mt19937_64 rng(random_device{}());
  static const char *hex = "0123456789abcdef";
  string out(40, '0');
  for (char &ch : out) {
    ch = hex[rng() % 16];
  }

  return out;
}

bool setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }

  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void backlogAppend(const string &bytes) {
  repl_backlog.append(bytes);
  if (repl_backlog.size() > 2 * REPL_BACKLOG_SIZE) {
    const size_t trim = repl_backlog.size() - REPL_BACKLOG_SIZE;
    repl_backlog.erase(0, trim);
    repl_backlog_start += trim;
  }

  g_master_repl_offset += static_cast<long long>(bytes.size());
}

string backlogSince(long long offset) {
  if (offset < static_cast<long long>(repl_backlog_start)) {
    return {};
  }

  const size_t start = static_cast<size_t>(offset - repl_backlog_start);
  if (start >= repl_backlog.size()) {
    return {};
  }

  return repl_backlog.substr(start);
}

void applyReplicaCommand(const vector<string> &argv) {
  if (g_databases == nullptr || argv.empty()) {
    return;
  }

  Client fake{};
  fake.fd = -1;
  CommandContext ctx{fake, *g_databases, nullptr, g_epoll_fd, true};
  g_aof_replaying = true;
  executeCommand(ctx, argv);
  g_aof_replaying = false;
}

bool tryFinishFullSync() {
  if (!full_sync_mode || g_databases == nullptr ||
      master_read_buffer.size() < full_sync_expected) {
    return false;
  }

  const string snapshot = master_read_buffer.substr(0, full_sync_expected);

  if (!loadRDBFromBuffer(snapshot, *g_databases)) {
    return false;
  }

  const string tail = master_read_buffer.substr(full_sync_expected);
  master_read_buffer.clear();
  full_sync_mode = false;
  full_sync_expected = 0;
  if (!tail.empty()) {
    master_parser.feed(tail.data(), tail.size());
    vector<string> argv;
    while (master_parser.tryParse(argv)) {
      applyReplicaCommand(argv);
    }
  }

  return true;
}

void processMasterBuffer() {
  while (true) {
    if (full_sync_mode) {
      tryFinishFullSync();
      return;
    }

    if (master_read_buffer.rfind("+FULLRESYNC", 0) == 0) {
      const size_t line_end = master_read_buffer.find("\r\n");
      if (line_end == string::npos) {
        return;
      }

      const string line = master_read_buffer.substr(0, line_end);
      master_read_buffer.erase(0, line_end + 2);
      const size_t last_space = line.rfind(' ');
      if (last_space == string::npos) {
        return;
      }

      full_sync_expected =
          static_cast<size_t>(stoull(line.substr(last_space + 1)));
      full_sync_mode = true;
      tryFinishFullSync();
      return;
    }

    if (master_read_buffer.rfind("+CONTINUE", 0) == 0) {
      const size_t line_end = master_read_buffer.find("\r\n");
      if (line_end == string::npos) {
        return;
      }

      master_read_buffer.erase(0, line_end + 2);
      master_parser.feed(master_read_buffer.data(), master_read_buffer.size());
      master_read_buffer.clear();
      vector<string> argv;
      while (master_parser.tryParse(argv)) {
        applyReplicaCommand(argv);
      }

      return;
    }

    if (!master_read_buffer.empty() && master_read_buffer[0] == '+') {
      const size_t line_end = master_read_buffer.find("\r\n");
      if (line_end == string::npos) {
        return;
      }

      master_read_buffer.erase(0, line_end + 2);
      continue;
    }

    break;
  }

  if (master_read_buffer.empty()) {
    return;
  }

  master_parser.feed(master_read_buffer.data(), master_read_buffer.size());
  master_read_buffer.clear();
  vector<string> argv;
  while (master_parser.tryParse(argv)) {
    applyReplicaCommand(argv);
  }
}

bool connectToMaster() {
  if (g_server_config.replicaof_host.empty()) {
    return false;
  }

  master_link_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (master_link_fd < 0) {
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(g_server_config.replicaof_port));
  if (inet_pton(AF_INET, g_server_config.replicaof_host.c_str(),
                &addr.sin_addr) <= 0) {
    close(master_link_fd);
    master_link_fd = -1;
    return false;
  }

  if (connect(master_link_fd, reinterpret_cast<sockaddr *>(&addr),
              sizeof(addr)) < 0) {
    close(master_link_fd);
    master_link_fd = -1;
    return false;
  }

  setNonBlocking(master_link_fd);
  master_parser = RespParser{};
  master_read_buffer.clear();
  full_sync_mode = false;

  const string replconf = encodeArray(
      {"REPLCONF", "listening-port", to_string(g_server_config.port)});
  const string psync = encodeArray({"PSYNC", "?", "-1"});
  send(master_link_fd, replconf.data(), replconf.size(), 0);
  send(master_link_fd, psync.data(), psync.size(), 0);
  return true;
}

void wakeReplicaClient(int fd) {
  if (g_epoll_fd < 0 || g_clients == nullptr) {
    return;
  }

  auto it = g_clients->find(fd);
  if (it == g_clients->end()) {
    return;
  }

  if (g_client_write_pending_cb != nullptr) {
    g_client_write_pending_cb(g_epoll_fd, it->second);
  } else {
    epoll_event event{};
    event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    if (!it->second.write_buf.empty()) {
      event.events |= EPOLLOUT;
    }

    event.data.fd = fd;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &event);
  }
}

void registerMasterLink(int epoll_fd) {
  if (master_link_fd < 0 || master_link_registered) {
    return;
  }

  epoll_event event{};
  event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
  event.data.fd = master_link_fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, master_link_fd, &event);
  master_link_registered = true;
}
} // namespace

void initReplication() {
  if (g_repl_id.empty()) {
    g_repl_id = randomReplId();
  }
}

void replicationSetContext(vector<RedisDb> *databases,
                           unordered_map<int, Client> *clients, int epoll_fd) {
  g_databases = databases;
  g_clients = clients;
  g_epoll_fd = epoll_fd;
}

void replicationStartReplica(int epoll_fd) {
  if (connectToMaster()) {
    registerMasterLink(epoll_fd);
  }
}

void replicationPeriodic(int epoll_fd) {
  if (g_server_config.readonly_replica && master_link_fd < 0) {
    if (connectToMaster()) {
      registerMasterLink(epoll_fd);
    }
  }
}

void replicationCleanupClient(int fd) { replicas.erase(fd); }

bool clientIsReplica(const Client &client) {
  return replicas.count(client.fd) > 0;
}

bool replicationIsMasterLink(int fd) { return fd == master_link_fd; }

bool replicationHandleCommand(Client &client, const vector<string> &argv,
                              string &reply) {
  if (argv.empty() || g_server_config.readonly_replica) {
    return false;
  }

  const string &cmd = argv[0];
  if (cmd == "REPLCONF") {
    reply = encodeOK();
    return true;
  }

  if (cmd == "PSYNC") {
    const string requested_id = argv.size() > 1 ? argv[1] : "?";
    const long long requested_offset = argv.size() > 2 ? stoll(argv[2]) : -1;
    replicas[client.fd] = ReplicaConn{requested_offset};

    if (requested_id != "?" && requested_id == g_repl_id) {
      const string delta = backlogSince(requested_offset);
      if (!delta.empty()) {
        reply = "+CONTINUE\r\n" + delta;
        replicas[client.fd].repl_offset = g_master_repl_offset;
        return true;
      }
    }

    if (g_databases != nullptr) {
      const string snapshot = serializeRDB(*g_databases);
      reply = "+FULLRESYNC " + g_repl_id + " " +
              to_string(g_master_repl_offset) + " " +
              to_string(snapshot.size()) + "\r\n" + snapshot;
      replicas[client.fd].repl_offset = g_master_repl_offset;
      return true;
    }

    reply = encodeError("ERR replication not ready");
    return true;
  }

  return false;
}

bool replicationPreflightWrite(const Client &client, string &err) {
  if (client.fd == -1) {
    return true;
  }

  if (g_server_config.readonly_replica && !clientIsReplica(client)) {
    err = encodeError("READONLY You can't write against a read only replica");
    return false;
  }

  return true;
}

void replicationFeedWrite(const vector<string> &argv) {
  if (g_server_config.readonly_replica || argv.empty() ||
      g_clients == nullptr) {
    return;
  }

  const string encoded = encodeArray(argv);
  backlogAppend(encoded);
  for (const auto &entry : replicas) {
    auto it = g_clients->find(entry.first);
    if (it != g_clients->end()) {
      it->second.write_buf += encoded;
      wakeReplicaClient(entry.first);
    }
  }
}

void replicationHandleMasterEvent(int epoll_fd) {
  (void)epoll_fd;
  if (master_link_fd < 0) {
    return;
  }

  char buffer[4096];
  while (true) {
    const ssize_t bytes = recv(master_link_fd, buffer, sizeof(buffer), 0);
    if (bytes > 0) {
      master_read_buffer.append(buffer, static_cast<size_t>(bytes));
      processMasterBuffer();
      continue;
    }

    if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
      close(master_link_fd);
      master_link_fd = -1;
      master_link_registered = false;
    }

    break;
  }
}

string replicationInfoSection() {
  const string role = g_server_config.readonly_replica ? "slave" : "master";
  string section = "# Replication\r\n";
  section += "role:" + role + "\r\n";
  section += "master_replid:" + g_repl_id + "\r\n";
  section += "master_repl_offset:" + to_string(g_master_repl_offset) + "\r\n";
  section += "connected_slaves:" + to_string(replicas.size()) + "\r\n";
  if (g_server_config.readonly_replica) {
    section += "master_host:" + g_server_config.replicaof_host + "\r\n";
    section +=
        "master_port:" + to_string(g_server_config.replicaof_port) + "\r\n";
    section +=
        "master_link_status:" + string(master_link_fd >= 0 ? "up" : "down") +
        "\r\n";
  }

  return section;
}
