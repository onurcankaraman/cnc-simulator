#pragma once
#include "transport/snapshot.hpp"
#include <functional>
#include <sqlite3.h>
namespace cnc {
class Store {
  sqlite3 *db_ = nullptr;
  bool healthy_ = true;
  std::string error_;
  int64_t last_sample_ = 0;
  bool execute(const std::string &sql);
  bool bind_exec(const std::string &sql, const std::vector<Json> &values);

public:
  explicit Store(const std::string &file);
  ~Store();
  bool healthy() const {
    return healthy_;
  }
  const std::string &error() const {
    return error_;
  }
  Json restore();
  bool write(const MachineSnapshot &, const Json &state, const Json &events, const Json &order,
             bool sample);
  void cleanup(int64_t now);
  std::function<std::string()> history_stream(int64_t from, int64_t to, int limit, int step);
  Json history(int64_t from, int64_t to, int limit, int step);
  Json events(int64_t from, int limit);
  Json orders(int64_t from);
};
} // namespace cnc
