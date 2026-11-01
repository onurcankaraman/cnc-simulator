#include "adapters/sqlite_store.hpp"
#include <memory>
#include <stdexcept>
namespace cnc {
bool Store::execute(const std::string &s) {
  int rc = sqlite3_exec(db_, s.c_str(), nullptr, nullptr, nullptr);
  if (rc != SQLITE_OK) {
    healthy_ = false;
    error_ = sqlite3_errmsg(db_);
  }
  return rc == SQLITE_OK;
}
bool Store::bind_exec(const std::string &sql, const std::vector<Json> &v) {
  sqlite3_stmt *st = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr);
  if (rc == SQLITE_OK) {
    for (size_t i = 0; i < v.size(); i++) {
      auto &x = v[i];
      if (x.is_string()) {
        auto s = x.get<std::string>();
        sqlite3_bind_text(st, i + 1, s.c_str(), s.size(), SQLITE_TRANSIENT);
      } else if (x.is_number_float())
        sqlite3_bind_double(st, i + 1, x.get<double>());
      else if (x.is_null())
        sqlite3_bind_null(st, i + 1);
      else
        sqlite3_bind_int64(st, i + 1, x.get<int64_t>());
    }
    rc = sqlite3_step(st);
  }
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) {
    healthy_ = false;
    error_ = sqlite3_errmsg(db_);
  }
  return rc == SQLITE_DONE;
}
Store::Store(const std::string &f) {
  if (sqlite3_open(f.c_str(), &db_) != SQLITE_OK)
    throw std::runtime_error("cannot open database");
  sqlite3_busy_timeout(db_, 100);
  execute("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA cache_size=-2048; PRAGMA "
          "wal_autocheckpoint=1000;");
  if (!execute(
          "CREATE TABLE IF NOT EXISTS telemetry(ts_ms INTEGER PRIMARY KEY,seq INTEGER NOT "
          "NULL,state_code INTEGER,job_id INTEGER,program_id INTEGER,tool_id INTEGER,spindle_rpm "
          "REAL,load_pct REAL,feed_mm_min REAL,x_mm REAL,y_mm REAL,z_mm REAL,vibration "
          "REAL,spindle_temp_c REAL,power_kw REAL,energy_kwh REAL,wear_pct REAL,coolant_flow "
          "REAL,alarm_code INTEGER,good_count INTEGER,scrap_count INTEGER,payload_json TEXT NOT "
          "NULL); CREATE TABLE IF NOT EXISTS events(id INTEGER PRIMARY KEY,ts_ms INTEGER,type "
          "TEXT,code INTEGER,severity INTEGER,state_from INTEGER,state_to INTEGER,details_json "
          "TEXT); CREATE INDEX IF NOT EXISTS event_time ON events(ts_ms); CREATE TABLE IF NOT "
          "EXISTS orders(order_id INTEGER PRIMARY KEY,program_id INTEGER,part_id "
          "INTEGER,target_qty INTEGER,good_qty INTEGER,scrap_qty INTEGER,started_ms "
          "INTEGER,completed_ms INTEGER,status TEXT); CREATE TABLE IF NOT EXISTS runtime_state(key "
          "TEXT PRIMARY KEY,value_json TEXT NOT NULL,updated_ms INTEGER);"))
    throw std::runtime_error(error_);
}
Store::~Store() {
  sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
  sqlite3_close(db_);
}
Json Store::restore() {
  sqlite3_stmt *st = nullptr;
  Json j;
  if (sqlite3_prepare_v2(db_, "SELECT value_json FROM runtime_state WHERE key='engine'", -1, &st,
                         nullptr) == SQLITE_OK &&
      sqlite3_step(st) == SQLITE_ROW)
    j = Json::parse(reinterpret_cast<const char *>(sqlite3_column_text(st, 0)));
  sqlite3_finalize(st);
  return j;
}
bool Store::write(const MachineSnapshot &s, const Json &state, const Json &ev, const Json &o,
                  bool sample) {
  healthy_ = true;
  if (!execute("BEGIN IMMEDIATE"))
    return false;
  bool ok = true;
  for (auto &e : ev) {
    ok = bind_exec("INSERT OR REPLACE INTO events VALUES(?,?,?,?,?,?,?,?)",
                   {e["event_id"], e["ts_ms"], e["type"], e["code"], e["severity"], e["state_from"],
                    e["state_to"], e.dump()}) &&
         ok;
    if (e.contains("order")) {
      auto old = e["order"];
      ok =
          bind_exec("INSERT OR REPLACE INTO orders VALUES(?,?,?,?,?,?,?,?,?)",
                    {old["order_id"], old["program_id"], old["part_id"], old["target_qty"],
                     old["good_qty"], old["scrap_qty"], old["started_ms"], s.ts_ms, "completed"}) &&
          ok;
    }
  }
  if (sample) {
    auto &d = s.data;
    ok = bind_exec(
             "INSERT OR REPLACE INTO telemetry VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
             {s.ts_ms,
              s.sequence,
              d["state_code"],
              d["job_id"],
              d["program_id"],
              d["current_tool_id"],
              d["spindle_actual_rpm"],
              d["spindle_load_pct"],
              d["feed_actual_mm_min"],
              d["axis_x_mm"],
              d["axis_y_mm"],
              d["axis_z_mm"],
              d["vibration_rms_mm_s"],
              d["spindle_temp_c"],
              d["power_kw"],
              d["energy_kwh"],
              d["active_wear_pct"],
              d["coolant_flow_l_min"],
              d["alarm_code"],
              d["total_good_count"],
              d["total_reject_count"],
              envelope(s, d).dump()}) &&
         ok;
  }
  ok = bind_exec("INSERT OR REPLACE INTO runtime_state VALUES('engine',?,?)",
                 {state.dump(), s.ts_ms}) &&
       ok;
  if (o["order_id"].get<int>() > 0)
    ok = bind_exec("INSERT OR REPLACE INTO orders VALUES(?,?,?,?,?,?,?,?,?)",
                   {o["order_id"], o["program_id"], o["part_id"], o["target_qty"], o["good_qty"],
                    o["scrap_qty"], o["started_ms"], o["completed_ms"], o["status"]}) &&
         ok;
  if (ok)
    ok = execute("COMMIT");
  if (!ok) {
    auto cause = error_;
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    error_ = cause;
  }
  healthy_ = ok;
  if (ok) {
    error_.clear();
    last_sample_ = s.ts_ms;
  }
  return ok;
}
void Store::cleanup(int64_t now) {
  bind_exec("DELETE FROM telemetry WHERE ts_ms<?", {now - 86400000});
  bind_exec("DELETE FROM events WHERE ts_ms<?", {now - 86400000});
  bind_exec("DELETE FROM orders WHERE status!='active' AND completed_ms<?", {now - 86400000});
  sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
}
Json Store::history(int64_t from, int64_t to, int limit, int step) {
  Json a = Json::array();
  sqlite3_stmt *st = nullptr;
  const char *sql = "SELECT payload_json FROM telemetry WHERE ts_ms>=? AND ts_ms<=? AND ts_ms IN "
                    "(SELECT MIN(ts_ms) FROM telemetry WHERE ts_ms>=? AND ts_ms<=? GROUP BY "
                    "ts_ms/?) ORDER BY ts_ms LIMIT ?";
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, from);
    sqlite3_bind_int64(st, 2, to);
    sqlite3_bind_int64(st, 3, from);
    sqlite3_bind_int64(st, 4, to);
    sqlite3_bind_int(st, 5, step * 1000);
    sqlite3_bind_int(st, 6, limit);
    while (sqlite3_step(st) == SQLITE_ROW)
      a.push_back(Json::parse(reinterpret_cast<const char *>(sqlite3_column_text(st, 0))));
  }
  sqlite3_finalize(st);
  return a;
}
Json Store::events(int64_t from, int limit) {
  Json a = Json::array();
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(
          db_, "SELECT details_json FROM events WHERE ts_ms>=? ORDER BY ts_ms DESC,id DESC LIMIT ?",
          -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, from);
    sqlite3_bind_int(st, 2, limit);
    while (sqlite3_step(st) == SQLITE_ROW)
      a.push_back(Json::parse(reinterpret_cast<const char *>(sqlite3_column_text(st, 0))));
  }
  sqlite3_finalize(st);
  return a;
}
Json Store::orders(int64_t from) {
  Json a = Json::array();
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT * FROM orders WHERE status='active' OR completed_ms>=? ORDER BY "
                         "order_id DESC LIMIT 5000",
                         -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, from);
    while (sqlite3_step(st) == SQLITE_ROW) {
      Json row;
      for (int i = 0; i < 9; i++)
        if (i == 8)
          row[sqlite3_column_name(st, i)] =
              reinterpret_cast<const char *>(sqlite3_column_text(st, i));
        else
          row[sqlite3_column_name(st, i)] = sqlite3_column_int64(st, i);
      a.push_back(row);
    }
  }
  sqlite3_finalize(st);
  return a;
}
} // namespace cnc

namespace cnc {
std::function<std::string()> Store::history_stream(int64_t from, int64_t to, int limit, int step) {
  sqlite3_stmt *raw = nullptr;
  const char *sql = "SELECT payload_json FROM telemetry WHERE ts_ms>=? AND ts_ms<=? AND ts_ms IN "
                    "(SELECT MIN(ts_ms) FROM telemetry WHERE ts_ms>=? AND ts_ms<=? GROUP BY "
                    "ts_ms/?) ORDER BY ts_ms LIMIT ?";
  if (sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr) != SQLITE_OK)
    throw std::runtime_error("history unavailable");
  std::shared_ptr<sqlite3_stmt> st(raw, sqlite3_finalize);
  sqlite3_bind_int64(raw, 1, from);
  sqlite3_bind_int64(raw, 2, to);
  sqlite3_bind_int64(raw, 3, from);
  sqlite3_bind_int64(raw, 4, to);
  sqlite3_bind_int(raw, 5, step * 1000);
  sqlite3_bind_int(raw, 6, limit);
  return [st, first = true, done = false]() mutable -> std::string {
    if (done)
      return {};
    std::string chunk = first ? "[" : "";
    while (chunk.size() < 16384) {
      int rc = sqlite3_step(st.get());
      if (rc == SQLITE_DONE) {
        chunk += "]";
        done = true;
        break;
      }
      if (rc != SQLITE_ROW)
        throw std::runtime_error("history read failed");
      if (!first)
        chunk += ",";
      chunk += reinterpret_cast<const char *>(sqlite3_column_text(st.get(), 0));
      first = false;
    }
    return chunk;
  };
}
} // namespace cnc
