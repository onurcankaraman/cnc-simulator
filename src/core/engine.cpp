#include "core/engine.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace cnc {
static double clip(double x, double lo, double hi) {
  return std::clamp(x, lo, hi);
}
static int step_code(const std::string &s) {
  static const std::vector<std::string> names = {"NONE",  "LOAD",      "RAPID",  "CUT",    "DRILL",
                                                 "DWELL", "TOOL_SWAP", "UNLOAD", "INSPECT"};
  auto i = std::find(names.begin(), names.end(), s);
  if (i == names.end())
    throw std::runtime_error("invalid NC step");
  return int(i - names.begin());
}
double Engine::random() {
  return std::generate_canonical<double, 53>(rng_);
}
Engine::Engine(Json config, uint64_t seed) : config_(std::move(config)), rng_(seed) {
  for (auto &t : tags)
    d_[t.key] = (t.kind == Kind::STRING32 || t.kind == Kind::STRING48) ? Json("") : Json(0);
  tools_ = config_["tools"]["tools"];
  for (auto &t : tools_) {
    t["wear_pct"] = t["initial_wear_pct"];
    t["cutting_minutes"] = 0.;
    t["stress_ema"] = 1.;
    t["replacement_count"] = 0;
    t["break_probability"] = 0.;
  }
  d_["spindle_temp_c"] = 24.;
  d_["coolant_temp_c"] = 24.;
  d_["door_closed"] = 1;
  d_["feed_override_pct"] = 100.;
  d_["spindle_override_pct"] = 100.;
  d_["magazine_tool_count"] = 12;
  d_["pending_job_count"] = 1;
  profile_ = config_["plant"]["simulation"]["default_profile"].get<std::string>();
  sync_tools();
}
void Engine::event(std::string type, int code, int severity, Json extra) {
  ++event_id_;
  d_["event_sequence"] = event_id_;
  d_["last_event_utc_s"] = now_ / 1000;
  Json e = {{"event_id", event_id_}, {"ts_ms", now_},
            {"type", type},          {"code", code},
            {"severity", severity},  {"start", utc(now_)},
            {"end", nullptr},        {"reason", d_["reason_code"]},
            {"duration_s", 0},       {"order_id", d_["job_id"]},
            {"tool_id", tool_},      {"state_from", state_},
            {"state_to", state_}};
  e.update(extra);
  if (events_.size() >= 256)
    throw std::runtime_error("critical event queue overflow");
  events_.push_back(e);
}
void Engine::transition(int next, int reason) {
  int from = state_;
  bool legal = next == 70 || next == 80 || next == 50 || next == 10 || next == 90 ||
               (from == 0 && next == 10) || (from == 10 && (next == 20 || next == 30)) ||
               (from == 20 && next == 30) || ((from == 30 || from == 50) && next == 40) ||
               (from == 40 && (next == 60 || next == 30)) ||
               (from == 60 && (next == 40 || next == 30)) ||
               ((from == 50 || from == 70 || from == 80 || from == 90) && next == 30);
  if (next != from && !legal)
    throw std::runtime_error("illegal state transition " + std::to_string(from) + " -> " +
                             std::to_string(next));
  state_ = next;
  d_["state_code"] = next;
  d_["reason_code"] = reason;
  d_["last_stop_reason"] = reason;
  if (next != from)
    event("state_change", 0, 0, {{"state_from", from}, {"state_to", next}});
}
void Engine::new_order() {
  d_["job_id"] = d_["job_id"].get<int>() + 1;
  auto &p = config_["programs"]["programs"][program_];
  d_["program_id"] = p["id"];
  d_["part_id"] = p["part_id"];
  auto r = config_["plant"]["simulation"]["work_order_target_qty"];
  d_["job_target_qty"] = int(double(r[0]) + random() * (double(r[1]) - double(r[0]) + 1));
  d_["job_good_qty"] = 0;
  d_["job_reject_qty"] = 0;
  d_["job_completed_qty"] = 0;
  d_["order_label"] = "WO-" + std::to_string(d_["job_id"].get<int>());
  d_["program_label"] = p["name"];
  d_["part_label"] = "PART-" + std::to_string(p["part_id"].get<int>());
  d_["nominal_cycle_s"] = p["nominal_cycle_seconds"];
  d_["cycle_target_s"] = p["nominal_cycle_seconds"];
  order_started_ = now_;
  auto setup = config_["plant"]["simulation"]["setup_duration_seconds"];
  timer_ = double(setup[0]) + random() * (double(setup[1]) - double(setup[0])) *
                                  (profile_ == "high_utilization" ? 0.4 : 1.);
  transition(20, 3);
  event("order_started");
}
bool Engine::planned_break() const {
  int second = int((now_ / 1000 + 10800 - 21600) % 28800);
  if (second < 0)
    second += 28800;
  for (auto &b : config_["plant"]["calendar"]["breaks_per_shift"]) {
    int start = int(double(b["offset_minutes"]) * 60),
        duration = int(double(b["duration_minutes"]) * 60);
    if (second >= start && second < start + duration)
      return true;
  }
  return false;
}
void Engine::begin_segment() {
  auto &s = config_["programs"]["programs"][program_]["segments"][segment_];
  elapsed_ = 0;
  int step = step_code(s["step"]);
  if (step == 6) {
    transition(60);
    event("tool_change_started");
  } else
    transition(40);
  d_["current_step_type"] = step;
  d_["current_block"] = segment_ + 1;
  d_["program_block"] = segment_ + 1;
}
void Engine::finish_segment() {
  auto &segs = config_["programs"]["programs"][program_]["segments"];
  if (int(d_["current_step_type"]) == 6) {
    tool_ = segs[segment_]["tool_id"].get<int>();
    d_["tool_change_counter"] = d_["tool_change_counter"].get<int>() + 1;
    event("tool_change");
  }
  if (int(d_["current_step_type"]) == 8) {
    bool reject = random() < clip(0.009 + cycle_risk_, 0, 0.3);
    d_[reject ? "job_reject_qty" : "job_good_qty"] =
        d_[reject ? "job_reject_qty" : "job_good_qty"].get<int>() + 1;
    d_[reject ? "total_reject_count" : "total_good_count"] =
        d_[reject ? "total_reject_count" : "total_good_count"].get<int>() + 1;
    d_["total_parts_count"] =
        d_["total_good_count"].get<int>() + d_["total_reject_count"].get<int>();
    d_["job_completed_qty"] = d_["job_good_qty"].get<int>() + d_["job_reject_qty"].get<int>();
    d_["cycle_sequence"] = d_["cycle_sequence"].get<int>() + 1;
    d_["last_reject_code"] = reject ? 1 : 0;
    d_["last_cycle_duration_s"] = cycle_;
    shift_total_++;
    if (!reject)
      shift_good_++;
    shift_ideal_ += double(d_["nominal_cycle_s"]);
    event(reject ? "part_rejected" : "part_completed");
    cycle_ = 0;
    cycle_risk_ = 0;
  }
  if (++segment_ >= int(segs.size())) {
    segment_ = 0;
    transition(30);
    return;
  }
  begin_segment();
}
void Engine::physics(double dt) {
  auto &s = config_["programs"]["programs"][program_]["segments"][segment_];
  int step = d_["current_step_type"];
  bool running = state_ == 40, moving = running && (step == 2 || step == 3 || step == 4);
  bool cutstep = step == 3 || step == 4;
  d_["controller_mode"] = state_ == 0 ? 0 : state_ == 20 ? 4 : 1;
  d_["door_closed"] = fault_ == 1401 ? 0 : 1;
  d_["estop_active"] = fault_ == 1402 ? 1 : 0;
  d_["part_clamped"] = state_ == 40 && step != 1 && step != 7 && fault_ != 1302 ? 1 : 0;
  double target = running ? double(s["rpm"]) : 0.;
  double rpm = d_["spindle_actual_rpm"];
  rpm += (target - rpm) * (1 - std::exp(-dt / (fault_ == 1402 ? 0.25 : 1.8)));
  d_["spindle_actual_rpm"] = rpm;
  d_["spindle_target_rpm"] = target;
  d_["spindle_on"] = rpm > 50 ? 1 : 0;
  double feed = moving ? double(s["feed_mm_min"]) * std::min(1., elapsed_ / 0.6) : 0.;
  d_["feed_command_mm_min"] = moving ? double(s["feed_mm_min"]) : 0.;
  d_["feed_actual_mm_min"] = feed;
  bool cutting = running && cutstep && rpm > 300 && feed > 0 && int(d_["part_clamped"]) &&
                 int(d_["door_closed"]);
  d_["is_cutting"] = cutting ? 1 : 0;
  d_["cycle_active"] = (state_ == 40 || state_ == 60 || state_ == 50) ? 1 : 0;
  const char *pos[] = {"axis_x_mm", "axis_y_mm", "axis_z_mm"};
  const char *vel[] = {"axis_x_velocity_mm_s", "axis_y_velocity_mm_s", "axis_z_velocity_mm_s"};
  double distance = 0;
  for (int i = 0; i < 3; i++) {
    double delta = double(s["target_xyz_mm"][i]) - double(d_[pos[i]]);
    distance += delta * delta;
  }
  distance = std::sqrt(distance);
  double remaining = std::max(dt, double(s["duration_s"]) * cycle_scale_ - elapsed_ + dt);
  double travel = moving ? std::min({distance, feed / 60 * dt, distance * dt / remaining}) : 0.;
  for (int i = 0; i < 3; i++) {
    double old = d_[pos[i]], goal = s["target_xyz_mm"][i];
    double delta = distance > 1e-9 ? (goal - old) * travel / distance : 0.;
    d_[pos[i]] = old + delta;
    d_[vel[i]] = std::abs(delta) / dt;
  }
  feed = travel / dt * 60;
  d_["feed_actual_mm_min"] = feed;
  cutting = cutting && feed > 1e-6;
  d_["is_cutting"] = cutting ? 1 : 0;
  noise_ = 0.96 * noise_ + 0.04 * (random() - 0.5) * 4;
  double wear = tool_ ? double(tools_[tool_ - 1]["wear_pct"]) : 0;
  double load = cutting ? double(s["load_pct"]) * (1 + 0.22 * wear / 100) + noise_ +
                              (step == 4 ? 3 * std::sin(elapsed_ * 4) : 0)
                        : (rpm > 300 ? 4 : 0.8);
  if (fault_ == 1101)
    load = 105;
  if (fault_ == 1003)
    load = 100;
  load = clip(load, 0, 110);
  d_["spindle_load_pct"] = load;
  double temp = d_["spindle_temp_c"];
  temp += (24 + load * 0.45 + (fault_ == 1102 ? 45 : 0) - temp) * dt / 600;
  d_["spindle_temp_c"] = clip(temp, 20, 90);
  double vibration =
      0.4 + rpm / 15000 + (cutting ? load / 90 + wear * wear / 8000 : 0) + noise_ * 0.05;
  if (fault_ == 1003)
    vibration = 24;
  d_["vibration_rms_mm_s"] = clip(vibration, 0, 30);
  bool coolant = running && s["coolant"].get<bool>();
  d_["coolant_on"] = coolant ? 1 : 0;
  double flow = coolant ? 32 : 0, pressure = coolant ? 4 : 0;
  if (fault_ == 1201)
    flow *= std::max(0.05, 1 - fault_elapsed_ / 10);
  if (fault_ == 1202) {
    flow = 2;
    pressure = 9;
  }
  d_["coolant_flow_l_min"] = double(d_["coolant_flow_l_min"]) +
                             (flow - double(d_["coolant_flow_l_min"])) * std::min(1., dt * 2);
  d_["coolant_pressure_bar"] =
      double(d_["coolant_pressure_bar"]) +
      (pressure - double(d_["coolant_pressure_bar"])) * std::min(1., dt * 2);
  d_["coolant_temp_c"] =
      double(d_["coolant_temp_c"]) + (24 + load * 0.08 - double(d_["coolant_temp_c"])) * dt / 900;
  d_["air_pressure_bar"] = fault_ == 1301 ? std::max(1., 6 - fault_elapsed_) : 6 + noise_ * 0.03;
  d_["hydraulic_pressure_bar"] =
      fault_ == 1302 ? std::max(5., 70 - 10 * fault_elapsed_) : 70 + noise_ * 0.2;
  d_["ambient_temp_c"] = 24 + 2 * std::sin(now_ / 86400000. * 6.283);
  d_["cabinet_temp_c"] = double(d_["ambient_temp_c"]) + 5 + load * 0.03;
  double power = 1.1 + rpm / 12000 * 2 + load / 100 * 12 + (moving ? 0.7 : 0) + (coolant ? 0.4 : 0);
  d_["power_kw"] = power;
  energy_ += power * dt / 3600;
  d_["energy_kwh"] = energy_;
  d_["data_quality_code"] = fault_ == 1702 ? 1 : 0;
  if (cutting && tool_) {
    auto &t = tools_[tool_ - 1];
    double mat = program_ == 1 ? 1.35 : program_ == 3 ? 0.75 : 1.;
    double stress = clip(mat * (0.6 + 0.4 * load / 75) *
                             (0.7 + 0.3 * feed / std::max(1., double(s["feed_mm_min"]))) *
                             (1 + std::max(0., temp - 60) / 80),
                         0.2, 3.);
    if (profile_ == "aging_tools")
      stress = std::min(3., stress * 1.8);
    t["stress_ema"] = double(t["stress_ema"]) + (stress - double(t["stress_ema"])) * dt / 30;
    double before = t["wear_pct"];
    double after =
        std::min(100., before + 100 * dt / (60 * double(t["nominal_life_min"])) * stress);
    t["wear_pct"] = after;
    t["cutting_minutes"] = double(t["cutting_minutes"]) + dt / 60;
    for (int threshold : {80, 95, 100})
      if (before < threshold && after >= threshold)
        event(threshold == 80   ? "ToolLifeWarning"
              : threshold == 95 ? "ToolReplacementDue"
                                : "ToolLifeLimit",
              threshold == 100 ? 1002 : 1001, threshold == 100 ? 3 : 1);
    t["break_probability"] = std::pow(after / 100, 6) * load / 100 * dt / 360000;
    if (after >= 100) {
      if (fault_ && fault_ != 1002) {
        event("fault_superseded_by_tool_limit", fault_, 2);
        fault_ = 0;
      }
      if (!fault_)
        command("inject-fault", {{"code", 1002}});
    } else if (!fault_ && random() < double(t["break_probability"]))
      command("inject-fault", {{"code", 1003}});
    cycle_risk_ =
        std::max(cycle_risk_, std::max(0., after - 70) / 1000 + std::max(0., vibration - 3) / 100 +
                                  (fault_ == 1201 || fault_ == 1202 ? 0.05 : 0) +
                                  (profile_ == "quality_issue" ? 0.06 : 0));
  }
}
void Engine::sync_tools() {
  for (int i = 0; i < 12; i++) {
    auto &t = tools_[i];
    double w = t["wear_pct"];
    t["life_remaining_minutes"] = std::max(0., double(t["nominal_life_min"]) * (1 - w / 100) /
                                                   std::max(0.2, double(t["stress_ema"])));
    t["diameter_wear_mm"] = double(t["max_diam_wear_mm"]) * std::pow(w / 100, 1.4);
    t["length_offset_mm"] = -0.02 * w / 100;
    char prefix[30];
    snprintf(prefix, sizeof(prefix), "tool_%02d_", i + 1);
    std::string p = prefix;
    d_[p + "tool_id"] = t["id"];
    d_[p + "tool_type"] = t["type_code"];
    for (auto k : {"wear_pct", "cutting_minutes", "life_remaining_minutes"})
      d_[p + k] = t[k];
  }
  d_["current_tool_id"] = tool_;
  d_["active_tool_id"] = tool_;
  d_["toolchange_inprogress"] = state_ == 60 ? 1 : 0;
  if (tool_) {
    auto &t = tools_[tool_ - 1];
    d_["active_wear_pct"] = t["wear_pct"];
    d_["active_life_remaining_min"] = t["life_remaining_minutes"];
    d_["active_cutting_minutes"] = t["cutting_minutes"];
    d_["active_diameter_wear_mm"] = t["diameter_wear_mm"];
    d_["active_length_offset_mm"] = t["length_offset_mm"];
    d_["active_load_factor"] = t["stress_ema"];
    d_["active_tool_type"] = t["type_code"];
    d_["active_health_code"] = double(t["wear_pct"]) >= 100  ? 3
                               : double(t["wear_pct"]) >= 80 ? 1
                                                             : 0;
    d_["replacement_due"] = double(t["wear_pct"]) >= 95 ? 1 : 0;
  }
}
void Engine::tick(double dt, int64_t wall) {
  if (!(dt > 0 && dt <= 1))
    throw std::runtime_error("invalid monotonic delta");
  now_ = wall;
  uptime_ += dt;
  if (prev_wall_ && wall < prev_wall_)
    event("wall_clock_regression", 1702, 1);
  prev_wall_ = wall;
  int64_t sk = (wall / 1000 + 10800 - 21600) / 28800;
  if (sk != shift_key_) {
    shift_key_ = sk;
    shift_run_ = shift_plan_ = shift_ideal_ = shift_good_ = shift_total_ = 0;
    event("shift_changed");
  }
  d_["shift_id"] = int((sk % 3 + 3) % 3) + 1;
  if (!planned_break())
    shift_plan_ += dt;
  if (!planned_break() && (state_ == 40 || state_ == 60))
    shift_run_ += dt;
  if (fault_) {
    fault_elapsed_ += dt;
    d_["alarm_duration_s"] = int(fault_elapsed_);
    if ((fault_ == 1201 || fault_ == 1202 || fault_ == 1301 || fault_ == 1302 || fault_ == 1102) &&
        fault_elapsed_ >= 5 && state_ != 70)
      transition(70, 6);
    if (fault_elapsed_ >= fault_duration_) {
      int old = fault_;
      event("fault_end", old, 0, {{"duration_s", fault_elapsed_}, {"end", utc(now_)}});
      if (old == 1002 || old == 1003) {
        command("replace-tool", {{"slot", tool_}});
      }
      fault_ = 0;
      d_["alarm_code"] = 0;
      d_["alarm_active"] = 0;
      d_["alarm_latched"] = 0;
      d_["alarm_acknowledged"] = 1;
      d_["alarm_severity"] = 0;
      d_["alarm_requires_operator"] = 0;
      d_["alarm_text"] = "";
      d_["maintenance_due"] = 0;
      if (old == 1602) {
        transition(40);
      } else if (old != 1502 && old != 1702) {
        event("simulated_operator_reset", old, 0);
        event("incomplete_cycle_discarded", old, 1);
        segment_ = 0;
        cycle_ = 0;
        transition(30);
      }
    }
  }
  if (!fault_ || fault_ == 1502 || fault_ == 1702 || (state_ == 40 && fault_elapsed_ < 5)) {
    if (state_ == 0) {
      transition(10, 1);
      timer_ = 2;
    } else if (state_ == 10) {
      timer_ -= dt;
      if (timer_ <= 0)
        new_order();
    } else if (state_ == 20) {
      timer_ -= dt;
      if (timer_ <= 0)
        transition(30);
    } else if (state_ == 90) {
      if (!planned_break())
        transition(30);
    } else if (state_ == 80) {
      timer_ -= dt;
      if (timer_ <= 0) {
        command("replace-tool", {{"slot", tool_}});
        transition(30);
      }
    } else if (state_ == 30) {
      if (int(d_["job_completed_qty"]) >= int(d_["job_target_qty"])) {
        event("order_completed", 0, 0, {{"order", order()}});
        program_ = (program_ + 1) % 4;
        transition(10, 1);
        int weekday = int(((now_ / 1000 + 10800) / 86400 + 4) % 7);
        double demand = (weekday == 0 || weekday == 6)
                            ? double(config_["plant"]["calendar"]["weekend_order_demand_factor"])
                            : 1.;
        timer_ = 2 + (1 - demand) * (300 + random() * 600);
      } else if (planned_break())
        transition(90, 2);
      else if (tool_ && double(tools_[tool_ - 1]["wear_pct"]) >= 95) {
        transition(80, 7);
        timer_ = 60 + random() * 120;
      } else {
        segment_ = 0;
        cycle_ = 0;
        cycle_scale_ = 1 - std::log(std::max(1e-9, 1 - random())) * 0.025;
        begin_segment();
      }
    } else if (state_ == 40 || state_ == 60) {
      elapsed_ += dt;
      cycle_ += dt;
      auto &s = config_["programs"]["programs"][program_]["segments"][segment_];
      if (elapsed_ >= double(s["duration_s"]) * cycle_scale_ &&
          (step_code(s["step"]) != 6 || double(d_["spindle_actual_rpm"]) < 50))
        finish_segment();
    }
    if (state_ == 40 && !fault_) {
      for (auto &f : config_["faults"]["faults"]) {
        double hazard = f["hazard_per_operating_hour"];
        if (profile_ == "high_utilization" && (int(f["code"]) == 1601 || int(f["code"]) == 1602))
          hazard *= 0.4;
        if ((profile_ == "night_shift" || int(d_["shift_id"]) == 3) && int(f["code"]) == 1601)
          hazard *= 2;
        if (profile_ == "unstable_coolant" && (int(f["code"]) == 1201 || int(f["code"]) == 1202))
          hazard *= 20;
        if (profile_ == "frequent_microstops" && int(f["code"]) == 1602)
          hazard *= 30;
        if (profile_ == "maintenance_week")
          hazard *= 4;
        if (random() < hazard * dt / 3600) {
          command("inject-fault", {{"code", f["code"]}});
          break;
        }
      }
    }
  }
  physics(dt);
  if (state_ != 40) {
    d_["is_cutting"] = 0;
    d_["feed_actual_mm_min"] = 0.;
    d_["spindle_target_rpm"] = 0.;
  }
  sync_tools();
  d_["cycle_elapsed_s"] = cycle_;
  d_["current_cycle_elapsed_s"] = cycle_;
  double progress = clip(cycle_ / std::max(1., double(d_["nominal_cycle_s"])) * 100, 0, 100);
  d_["program_progress_pct"] = progress;
  d_["part_progress_pct"] = progress;
  d_["job_progress_pct"] =
      double(d_["job_completed_qty"]) / std::max(1., double(d_["job_target_qty"])) * 100;
  d_["estimated_job_remaining_s"] =
      (double(d_["job_target_qty"]) - double(d_["job_completed_qty"])) *
      double(d_["nominal_cycle_s"]);
  d_["job_state_code"] = state_ == 20 ? 1 : 2;
  d_["availability_state"] = state_ == 70 ? 0 : 1;
  d_["quality_state"] = fault_ == 1702 ? 1 : 0;
}
MachineSnapshot Engine::snapshot() {
  MachineSnapshot s;
  s.sequence = ++seq_;
  s.ts_ms = now_;
  d_["sequence"] = uint32_t(seq_);
  d_["unix_utc_s"] = now_ / 1000;
  d_["last_heartbeat_utc_s"] = now_ / 1000;
  s.data = d_;
  s.tools = tools_;
  double a = shift_plan_ ? shift_run_ / shift_plan_ : 0,
         p = shift_run_ ? shift_ideal_ / shift_run_ : 0,
         q = shift_total_ ? shift_good_ / shift_total_ : 1;
  s.metrics = {{"uptime_s", uptime_}, {"availability", a}, {"performance", p},
               {"quality", q},        {"oee", a * p * q},  {"profile", profile_}};
  canonicalize(s);
  return s;
}
Json Engine::take_events() {
  Json result = std::move(events_);
  events_ = Json::array();
  return result;
}
Json Engine::order() const {
  return {{"order_id", d_["job_id"]},
          {"program_id", d_["program_id"]},
          {"part_id", d_["part_id"]},
          {"target_qty", d_["job_target_qty"]},
          {"good_qty", d_["job_good_qty"]},
          {"scrap_qty", d_["job_reject_qty"]},
          {"started_ms", order_started_},
          {"completed_ms", int(d_["job_completed_qty"]) >= int(d_["job_target_qty"]) ? now_ : 0},
          {"status",
           int(d_["job_completed_qty"]) >= int(d_["job_target_qty"]) ? "completed" : "active"}};
}
Json Engine::save() const {
  std::ostringstream r;
  r << rng_;
  return {{"data", d_},
          {"tools", tools_},
          {"rng", r.str()},
          {"sequence", seq_},
          {"event_id", event_id_},
          {"program", program_},
          {"tool", tool_},
          {"energy", energy_},
          {"order_started", order_started_},
          {"profile", profile_},
          {"shift_key", shift_key_},
          {"shift_run", shift_run_},
          {"shift_plan", shift_plan_},
          {"shift_ideal", shift_ideal_},
          {"shift_good", shift_good_},
          {"shift_total", shift_total_}};
}
void Engine::restore(const Json &s, int64_t now) {
  d_ = s.at("data");
  tools_ = s.at("tools");
  std::istringstream r(s.at("rng").get<std::string>());
  r >> rng_;
  seq_ = s.at("sequence");
  event_id_ = s.at("event_id");
  program_ = s.at("program");
  tool_ = s.at("tool");
  energy_ = s.at("energy");
  order_started_ = s.at("order_started");
  profile_ = s.at("profile");
  shift_key_ = s.value("shift_key", int64_t(-1));
  shift_run_ = s.value("shift_run", 0.);
  shift_plan_ = s.value("shift_plan", 0.);
  shift_ideal_ = s.value("shift_ideal", 0.);
  shift_good_ = s.value("shift_good", 0.);
  shift_total_ = s.value("shift_total", 0.);
  now_ = now;
  state_ = int(d_["state_code"]);
  event("restart_interruption", 0, 1);
  event("sample_gap", 0, 1);
  fault_ = 0;
  d_["alarm_active"] = 0;
  d_["alarm_code"] = 0;
  d_["alarm_text"] = "";
  d_["spindle_actual_rpm"] = 0.;
  d_["feed_actual_mm_min"] = 0.;
  d_["is_cutting"] = 0;
  state_ = 80;
  transition(30, 11);
}
void Engine::command(const std::string &action, const Json &b) {
  if (action == "scenario") {
    auto p = b.at("profile").get<std::string>();
    std::vector<std::string> valid = {"normal",           "aging_tools",         "high_utilization",
                                      "unstable_coolant", "frequent_microstops", "quality_issue",
                                      "night_shift",      "maintenance_week"};
    if (std::find(valid.begin(), valid.end(), p) == valid.end())
      throw std::invalid_argument("unknown profile");
    profile_ = p;
    event("scenario_changed");
  } else if (action == "set-seed") {
    if (!b.at("seed").is_number_unsigned() &&
        (!b.at("seed").is_number_integer() || b.at("seed").get<int64_t>() < 0))
      throw std::invalid_argument("seed must be nonnegative integer");
    rng_.seed(b.at("seed").get<uint64_t>());
    event("seed_changed");
  } else if (action == "replace-tool") {
    if (!b.at("slot").is_number_integer())
      throw std::invalid_argument("slot must be integer");
    int slot = b.at("slot");
    if (slot < 1 || slot > 12)
      throw std::invalid_argument("slot must be 1..12");
    if (state_ == 40 || state_ == 60)
      throw std::invalid_argument("stop before replacing tool");
    auto &t = tools_[slot - 1];
    t["wear_pct"] = 0.;
    t["cutting_minutes"] = 0.;
    t["stress_ema"] = 1.;
    t["replacement_count"] = int(t["replacement_count"]) + 1;
    event("tool_replaced");
    sync_tools();
  } else if (action == "ack-alarm") {
    d_["alarm_acknowledged"] = 1;
    event("alarm_acknowledged");
  } else if (action == "trigger-workorder") {
    if (state_ == 40 || state_ == 60 || fault_)
      throw std::invalid_argument("workorder requires idle machine");
    event("order_cancelled", 0, 1, {{"order", order()}});
    transition(10, 3);
    timer_ = 0;
  } else if (action == "inject-fault") {
    if (fault_)
      throw std::invalid_argument("fault already active");
    if (!b.at("code").is_number_integer())
      throw std::invalid_argument("code must be integer");
    int code = b.at("code");
    if ((code == 1002 || code == 1003) && tool_ == 0)
      throw std::invalid_argument("tool fault requires an active tool");
    Json f;
    for (auto &x : config_["faults"]["faults"])
      if (int(x["code"]) == code)
        f = x;
    if (f.is_null() && code != 1002 && code != 1003 && code != 1701)
      throw std::invalid_argument("unknown fault code");
    fault_ = code;
    fault_elapsed_ = 0;
    fault_duration_ =
        f.is_null() ? 120
                    : double(f["min_duration_s"]) +
                          random() * (double(f["max_duration_s"]) - double(f["min_duration_s"]));
    int severity = f.is_null() ? 3 : int(f["severity"]);
    d_["alarm_code"] = code;
    d_["last_alarm_code"] = code;
    d_["alarm_active"] = 1;
    d_["alarm_latched"] = 1;
    d_["alarm_acknowledged"] = 0;
    d_["alarm_severity"] = severity;
    d_["alarm_requires_operator"] = severity >= 2 ? 1 : 0;
    d_["alarm_started_unix_s"] = now_ / 1000;
    d_["alarm_text"] = f.is_null() ? "TOOL_OR_POWER_FAULT" : f["name"];
    event("fault_start", code, severity);
    if (code == 1601)
      transition(10, 9);
    else if (code == 1602)
      transition(50, 4);
    else if (code == 1801)
      transition(50, 10);
    else if (code == 1502 || code == 1702) {
    } else if (code != 1201 && code != 1202 && code != 1301 && code != 1302 && code != 1102)
      transition(70, 6);
  } else
    throw std::invalid_argument("unknown command");
}
} // namespace cnc
