#pragma once
#include "transport/snapshot.hpp"
#include <functional>
#include <libpq-fe.h>
#include <chrono>
namespace cnc {
class Store {
  PGconn *db_ = nullptr;
  std::string connection_, password_, error_;
  bool healthy_ = false;
  std::chrono::steady_clock::time_point retry_{};
  bool connect();
  PGresult *query(const std::string &, const std::vector<Json> & = {});
  bool execute(const std::string &, const std::vector<Json> & = {});
public:
  explicit Store(const std::string &connection, const std::string &password = "");
  ~Store();
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;
  bool healthy() const { return healthy_; }
  const std::string &error() const { return error_; }
  Json restore();
  bool write(const MachineSnapshot &, const Json &, const Json &, const Json &, bool);
  void cleanup(int64_t now);
  std::function<std::string()> history_stream(int64_t from, int64_t to, int limit, int step);
  Json history(int64_t from, int64_t to, int limit, int step);
  Json events(int64_t from, int limit);
  Json orders(int64_t from);
};
}
