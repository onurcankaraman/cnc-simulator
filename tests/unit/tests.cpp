#include "adapters/sqlite_store.hpp"
#include "core/engine.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
using namespace cnc;
void check(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
constexpr int64_t epoch = 1793516400000;
struct Rig {
  Engine e;
  int64_t ticks = 0;
  Rig(const Json &c, int seed = 42) : e(c, seed) {}
  void tick() {
    e.tick(.2, epoch + ticks++ * 200);
    e.take_events();
  }
  void until(int state) {
    for (int i = 0; i < 100000; i++) {
      tick();
      if (int(e.data()["state_code"]) == state)
        return;
    }
    throw std::runtime_error("state timeout");
  }
  void cut() {
    for (int i = 0; i < 100000; i++) {
      tick();
      if (int(e.data()["is_cutting"]))
        return;
    }
    throw std::runtime_error("cut timeout");
  }
};
int main() {
  try {
    auto cfg = load_config("config");
    Engine a(cfg, 42), b(cfg, 42);
    int parts = 0;
    double energy = 0;
    std::set<int> states;
    for (int i = 0; i < 100000; i++) {
      auto old = a.data();
      a.tick(.2, epoch + i * 200);
      b.tick(.2, epoch + i * 200);
      for (const auto &axis : {std::string("x"), std::string("y"), std::string("z")}) {
        auto pos = "axis_" + axis + "_mm", velocity = "axis_" + axis + "_velocity_mm_s";
        check(std::abs(std::abs(double(a.data()[pos]) - double(old[pos])) / .2 -
                       double(a.data()[velocity])) < 1e-6,
              "axis derivative consistency");
      }
      double rpm_bound = 12000 * (1 - std::exp(-.2 / (int(a.data()["estop_active"]) ? .25 : 1.8)));
      check(std::abs(double(a.data()["spindle_actual_rpm"]) - double(old["spindle_actual_rpm"])) <=
                rpm_bound + 1e-6,
            "spindle acceleration bound");
      if (i % 5 == 0) {
        auto x = a.snapshot(), y = b.snapshot();
        check(x.data == y.data, "deterministic replay");
        states.insert(int(x.data["state_code"]));
        auto &d = x.data;
        check(int(d["total_parts_count"]) ==
                  int(d["total_good_count"]) + int(d["total_reject_count"]),
              "counters");
        check(double(d["energy_kwh"]) >= energy, "energy monotonic");
        energy = d["energy_kwh"];
        if (int(d["is_cutting"]))
          check(int(d["state_code"]) == 40 && int(d["door_closed"]) &&
                    double(d["feed_actual_mm_min"]) > 0,
                "cut invariant");
        parts = d["total_parts_count"];
        auto db = encode_s7(x);
        check(db[1].size() == 256 && db[3].size() == 128, "DB lengths");
      }
      a.take_events();
      b.take_events();
    }
    check(parts > 1 && states.count(20) && states.count(40) && states.count(60),
          "production states");
    for (int state : {10, 20, 30, 40, 60, 70, 80, 90}) {
      auto saved = a.save();
      saved["data"]["state_code"] = state;
      Engine reboot(cfg, 9);
      reboot.restore(saved, epoch + 20000000);
      check(reboot.data()["total_parts_count"] == a.data()["total_parts_count"], "reboot count");
      check(reboot.tools() == a.tools(), "reboot tools");
    }
    Rig warning(cfg);
    warning.cut();
    double elapsed = warning.e.data()["cycle_elapsed_s"];
    warning.e.command("inject-fault", {{"code", 1702}});
    for (int i = 0; i < 20; i++)
      warning.tick();
    check(double(warning.e.data()["cycle_elapsed_s"]) > elapsed,
          "warning must not freeze NC clock");
    Rig idle(cfg);
    idle.until(20);
    auto wear = idle.e.tools();
    for (int i = 0; i < 50; i++)
      idle.tick();
    for (int i = 0; i < 12; i++)
      check(wear[i]["wear_pct"] == idle.e.tools()[i]["wear_pct"], "idle wear drift");
    for (int code : {1002, 1003, 1101, 1102, 1201, 1202, 1301, 1302, 1401, 1402, 1501, 1502, 1601,
                     1602, 1701, 1702, 1801}) {
      Rig f(cfg);
      f.cut();
      auto count = f.e.data()["total_parts_count"];
      f.e.command("inject-fault", {{"code", code}});
      for (int i = 0; i < 30; i++)
        f.tick();
      if (code != 1502 && code != 1702) {
        check(!int(f.e.data()["is_cutting"]), "fault must stop cutting");
        check(double(f.e.data()["feed_actual_mm_min"]) == 0, "fault feed interlock");
      }
      for (int i = 0; i < 15000 && int(f.e.data()["alarm_active"]); i++)
        f.tick();
      check(!int(f.e.data()["alarm_active"]), "fault recovery");
      f.e.snapshot();
    }
    Rig threshold(cfg);
    threshold.cut();
    auto saved = threshold.e.save();
    int tool = saved["tool"];
    saved["tools"][tool - 1]["wear_pct"] = 99.9999;
    Engine limit(cfg, 4);
    limit.restore(saved, epoch);
    for (int i = 0; i < 3000; i++) {
      limit.tick(.2, epoch + i * 200);
      auto s = limit.snapshot();
      if (int(s.data["state_code"]) == 70)
        check(!int(s.data["is_cutting"]) && double(s.data["feed_actual_mm_min"]) == 0,
              "limit transition invariant");
      limit.take_events();
    }
    auto worn_cfg = cfg;
    for (auto &t : worn_cfg["tools"]["tools"]) {
      t["initial_wear_pct"] = 79.99;
      t["nominal_life_min"] = .1;
    }
    Engine worn(worn_cfg, 77);
    std::set<std::string> thresholds;
    bool warning_injected = false;
    for (int i = 0; i < 10000 && thresholds.size() < 3; i++) {
      worn.tick(.2, epoch + i * 200);
      auto snap = worn.snapshot();
      if (!warning_injected && int(snap.data["is_cutting"])) {
        worn.command("inject-fault", {{"code", 1702}});
        warning_injected = true;
      }
      if (double(snap.data["active_wear_pct"]) >= 100) {
        check(int(snap.data["alarm_code"]) == 1002 && !int(snap.data["is_cutting"]),
              "100 percent limit must preempt warning");
      }
      if (int(snap.data["state_code"]) == 70)
        check(!int(snap.data["is_cutting"]), "wear limit immediate stop");
      for (auto &ev : worn.take_events()) {
        std::string kind = ev["type"];
        if (kind == "ToolLifeWarning" || kind == "ToolReplacementDue" || kind == "ToolLifeLimit")
          thresholds.insert(kind);
      }
    }
    check(thresholds.size() == 3, "80/95/100 wear threshold events");
    std::remove("/tmp/cnc-unit.sqlite");
    Store db("/tmp/cnc-unit.sqlite");
    auto s = a.snapshot();
    Json retained_events = Json::array({{{"event_id", 900000},
                                         {"ts_ms", s.ts_ms},
                                         {"type", "retention_fixture"},
                                         {"code", 0},
                                         {"severity", 0},
                                         {"state_from", 10},
                                         {"state_to", 20}}});
    check(db.write(s, a.save(), retained_events, a.order(), true), "SQL write");
    auto completed_order = a.order();
    completed_order["order_id"] = 99999;
    completed_order["status"] = "completed";
    completed_order["completed_ms"] = s.ts_ms;
    check(db.write(s, a.save(), Json::array(), completed_order, false), "completed order insert");
    check(db.events(0, 10).size() == 1, "event stored");
    check(db.history(s.ts_ms - 1000, s.ts_ms, 10, 1).size() == 1, "history");
    auto stream = db.history_stream(s.ts_ms - 1000, s.ts_ms, 10, 1);
    std::string body;
    for (;;) {
      auto chunk = stream();
      if (chunk.empty())
        break;
      body += chunk;
    }
    check(Json::parse(body).size() == 1, "streamed JSON");
    stream = {};
    db.cleanup(s.ts_ms + 86460001);
    check(db.history(0, s.ts_ms, 10, 1).empty(), "24h retention");
    check(!db.restore().is_null(), "runtime retention");
    check(db.events(0, 10).empty(), "24h event retention");
    auto remaining_orders = db.orders(0);
    check(remaining_orders.size() == 1 && remaining_orders[0]["status"] == "active",
          "active order survives; old completed order removed");
    std::cout << "PASS deterministic 100000 ticks, " << parts
              << " parts; restart from 8 states; 17 fault/recovery paths; warning clock; idle "
                 "wear; stop invariants; SQL retention and streaming\n";
    Engine stress(cfg, 2026);
    std::ofstream traces("/tmp/cnc-traces.csv");
    traces << "seconds,load,temp,rpm,vibration,wear,power,energy,parts,program,tool,cutting\n";
    int stress_parts = 0;
    double integrated = 0;
    for (int i = 0; i < 3000000 && stress_parts < 1000; i++) {
      stress.tick(.2, epoch + i * 200);
      integrated += double(stress.data()["power_kw"]) * .2 / 3600;
      if (i % 25 == 0) {
        auto x = stress.snapshot();
        auto &d = x.data;
        stress_parts = d["total_parts_count"];
        check(int(d["job_completed_qty"]) == int(d["job_good_qty"]) + int(d["job_reject_qty"]),
              "stress job invariant");
        if (int(d["is_cutting"]))
          check(int(d["state_code"]) == 40 && int(d["door_closed"]), "stress interlock");
        if (i % 300 == 0)
          traces << i * .2 << "," << d["spindle_load_pct"] << "," << d["spindle_temp_c"] << ","
                 << d["spindle_actual_rpm"] << "," << d["vibration_rms_mm_s"] << ","
                 << d["active_wear_pct"] << "," << d["power_kw"] << "," << d["energy_kwh"] << ","
                 << stress_parts << "," << d["program_id"] << "," << d["current_tool_id"] << ","
                 << d["is_cutting"] << "\n";
      }
      stress.take_events();
    }
    check(stress_parts >= 1000, "1000 cycle stress");
    double error = std::abs(double(stress.data()["energy_kwh"]) - integrated) / integrated;
    check(error < .01, "power integral");
    std::cout << "PASS 1000 completed cycles; finite contract ranges; production invariants; power "
                 "integral relative error="
              << error << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << "\n";
    return 1;
  }
}
