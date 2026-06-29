#pragma once

#include "object.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

using Expires = std::unordered_map<std::string, long long>;

struct RedisDb {
  Db data;
  Expires expires;
};

extern long long g_cached_time_ms;

inline long long nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
      .count();
}

inline bool keyExists(const RedisDb &db, const std::string &key) {
  return db.data.find(key) != db.data.end();
}

inline bool setExpireAtMs(RedisDb &db, const std::string &key,
                          long long expire_ms) {
  if (!keyExists(db, key)) {
    return false;
  }

  db.expires[key] = expire_ms;
  return true;
}

inline bool removeExpire(RedisDb &db, const std::string &key) {
  return db.expires.erase(key) > 0;
}

inline long long ttlMilliseconds(const RedisDb &db, const std::string &key) {
  if (!keyExists(db, key)) {
    return -2;
  }

  auto it = db.expires.find(key);
  if (it == db.expires.end()) {
    return -1;
  }

  long long now = (g_cached_time_ms > 0) ? g_cached_time_ms : nowMs();
  long long remaining = it->second - now;
  return remaining > 0 ? remaining : 0;
}

inline long long ttlSeconds(const RedisDb &db, const std::string &key) {
  long long pttl = ttlMilliseconds(db, key);
  if (pttl < 0) {
    return pttl;
  }

  return pttl / 1000;
}

inline bool isExpired(const RedisDb &db, const std::string &key) {
  auto it = db.expires.find(key);
  long long now = (g_cached_time_ms > 0) ? g_cached_time_ms : nowMs();
  return it != db.expires.end() && it->second <= now;
}

inline bool expireIfNeeded(RedisDb &db, const std::string &key) {
  if (!isExpired(db, key)) {
    return false;
  }

  auto item = db.data.find(key);
  if (item != db.data.end()) {
    destroyObject(item->second);
    db.data.erase(item);
  }
  db.expires.erase(key);
  return true;
}

inline size_t activeExpireCycle(RedisDb &db, size_t sample_size = 20) {
  size_t total_expired = 0;
  long long now = (g_cached_time_ms > 0) ? g_cached_time_ms : nowMs();

  while (!db.expires.empty()) {
    size_t checked = 0;
    size_t expired = 0;
    auto it = db.expires.begin();

    while (it != db.expires.end() && checked < sample_size) {
      ++checked;
      if (it->second <= now) {
        auto item = db.data.find(it->first);
        if (item != db.data.end()) {
          destroyObject(item->second);
          db.data.erase(item);
        }
        it = db.expires.erase(it);
        ++expired;
      } else {
        ++it;
      }
    }

    total_expired += expired;
    if (checked == 0 || expired * 4 < checked) {
      break;
    }
  }

  return total_expired;
}

