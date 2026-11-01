#pragma once
#include "transport/snapshot.hpp"
#include <deque>
#include <random>
namespace cnc {
class Engine {
  Json config_, d_, tools_, events_ = Json::array();
  std::mt19937_64 rng_;
  uint64_t seq_ = 0, event_id_ = 0;
  int state_ = 0, program_ = 0, segment_ = 0, tool_ = 0, fault_ = 0;
  double timer_ = 0, elapsed_ = 0, cycle_ = 0, uptime_ = 0, noise_ = 0, energy_ = 0, shift_run_ = 0,
         shift_plan_ = 0, shift_ideal_ = 0, shift_good_ = 0, shift_total_ = 0, cycle_risk_ = 0,
         cycle_scale_ = 1;
  int64_t now_ = 0, prev_wall_ = 0, shift_key_ = -1, order_started_ = 0;
  double fault_elapsed_ = 0, fault_duration_ = 0;
  std::string profile_ = "normal";
  double random();
  void transition(int next, int reason = 0);
  void new_order();
  void begin_segment();
  void sync_tools();
  void physics(double dt);
  void finish_segment();
  bool planned_break() const;
  void event(std::string type, int code = 0, int severity = 0, Json extra = Json::object());

public:
  explicit Engine(Json config, uint64_t seed = 1);
  void tick(double dt, int64_t utc_ms);
  MachineSnapshot snapshot();
  Json save() const;
  void restore(const Json &state, int64_t now);
  Json take_events();
  Json order() const;
  void command(const std::string &action, const Json &body);
  void audit(const std::string &action) {
    event("admin_command", 0, 0, {{"action", action}});
  }
  const Json &data() const {
    return d_;
  }
  const Json &tools() const {
    return tools_;
  }
};
} // namespace cnc
