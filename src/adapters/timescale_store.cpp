#include "adapters/timescale_store.hpp"
#include <memory>
#include <stdexcept>
namespace cnc {
namespace {
PGconn *open(const std::string &dsn, const std::string &password) {
  const char *keys[] = {"dbname", "password", "connect_timeout", "options", nullptr};
  const char *values[] = {dsn.c_str(), password.empty() ? nullptr : password.c_str(), "2",
    "-c statement_timeout=1000 -c lock_timeout=100 -c idle_in_transaction_session_timeout=10000", nullptr};
  return PQconnectdbParams(keys, values, 1);
}
std::string params(int n) {
  std::string r;
  for (int i=1;i<=n;++i) r += (i==1?"":",") + std::string("$") + std::to_string(i);
  return r;
}
const std::string order_sql = "INSERT INTO orders VALUES("+params(9)+") ON CONFLICT(order_id) DO UPDATE SET program_id=excluded.program_id,part_id=excluded.part_id,target_qty=excluded.target_qty,good_qty=excluded.good_qty,scrap_qty=excluded.scrap_qty,started_ms=excluded.started_ms,completed_ms=excluded.completed_ms,status=excluded.status";
}
Store::Store(const std::string &s, const std::string &p):connection_(s),password_(p) {
  if (!connect()) throw std::runtime_error("TimescaleDB unavailable at startup; refusing to reset persistent counters");
}
Store::~Store() { if(db_) PQfinish(db_); }
bool Store::connect() {
  if(db_ && PQstatus(db_)==CONNECTION_OK) return true;
  if(std::chrono::steady_clock::now()<retry_) return false;
  if(db_) PQfinish(db_);
  db_=open(connection_,password_);
  healthy_=PQstatus(db_)==CONNECTION_OK;
  if(!healthy_) { error_="TimescaleDB connection unavailable"; retry_=std::chrono::steady_clock::now()+std::chrono::seconds(5); }
  return healthy_;
}
PGresult *Store::query(const std::string &sql,const std::vector<Json> &v) {
  if(!db_ || PQstatus(db_)!=CONNECTION_OK) { healthy_=false; error_="TimescaleDB connection unavailable"; return nullptr; }
  std::vector<std::string> strings; std::vector<const char*> values;
  for(const auto &x:v) strings.push_back(x.is_string()?x.get<std::string>():x.dump());
  for(size_t i=0;i<v.size();++i) values.push_back(v[i].is_null()?nullptr:strings[i].c_str());
  auto r=PQexecParams(db_,sql.c_str(),values.size(),nullptr,values.data(),nullptr,nullptr,0);
  if(!r || (PQresultStatus(r)!=PGRES_COMMAND_OK && PQresultStatus(r)!=PGRES_TUPLES_OK)) {
    healthy_=false;
    // SQLSTATE only: never log connection strings or parameter contents.
    const char *code=r?PQresultErrorField(r,PG_DIAG_SQLSTATE):nullptr;
    error_="TimescaleDB query failed"+std::string(code?": ":"")+(code?code:"");
    if(r) PQclear(r);
    return nullptr;
  }
  return r;
}
bool Store::execute(const std::string &sql,const std::vector<Json> &v) {
  auto r=query(sql,v); if(!r) return false; PQclear(r); return true;
}
Json Store::restore() {
  auto r=query("SELECT value_json FROM runtime_state WHERE key='engine'");
  if(!r) throw std::runtime_error("Cannot restore durable state; startup aborted");
  std::unique_ptr<PGresult,decltype(&PQclear)> result(r,PQclear);
  return PQntuples(r)?Json::parse(PQgetvalue(r,0,0)):Json();
}
bool Store::write(const MachineSnapshot &s,const Json &state,const Json &ev,const Json &o,bool sample) {
  if(!connect() || !execute("BEGIN")) return false;
  bool ok=true;
  for(const auto &e:ev) {
    ok=execute("INSERT INTO events VALUES("+params(8)+") ON CONFLICT(ts_ms,id) DO NOTHING",
      {e["event_id"],e["ts_ms"],e["type"],e["code"],e["severity"],e["state_from"],e["state_to"],e.dump()}) && ok;
    if(e.contains("order")) { auto old=e["order"]; ok=execute(order_sql,{old["order_id"],old["program_id"],old["part_id"],old["target_qty"],old["good_qty"],old["scrap_qty"],old["started_ms"],s.ts_ms,"completed"}) && ok; }
  }
  if(sample) {
    const auto &d=s.data;
    ok=execute("INSERT INTO telemetry VALUES("+params(22)+") ON CONFLICT(ts_ms) DO NOTHING",
      {s.ts_ms,s.sequence,d["state_code"],d["job_id"],d["program_id"],d["current_tool_id"],d["spindle_actual_rpm"],d["spindle_load_pct"],d["feed_actual_mm_min"],d["axis_x_mm"],d["axis_y_mm"],d["axis_z_mm"],d["vibration_rms_mm_s"],d["spindle_temp_c"],d["power_kw"],d["energy_kwh"],d["active_wear_pct"],d["coolant_flow_l_min"],d["alarm_code"],d["total_good_count"],d["total_reject_count"],envelope(s,d).dump()}) && ok;
  }
  ok=execute("INSERT INTO runtime_state VALUES('engine',$1,$2) ON CONFLICT(key) DO UPDATE SET value_json=excluded.value_json,updated_ms=excluded.updated_ms",{state.dump(),s.ts_ms}) && ok;
  if(o["order_id"].get<int>()>0) ok=execute(order_sql,{o["order_id"],o["program_id"],o["part_id"],o["target_qty"],o["good_qty"],o["scrap_qty"],o["started_ms"],o["completed_ms"],o["status"]}) && ok;
  if(ok) ok=execute("COMMIT");
  if(!ok) { auto cause=error_; if(db_ && PQstatus(db_)==CONNECTION_OK) {auto r=PQexec(db_,"ROLLBACK");PQclear(r);} error_=cause; }
  healthy_=ok; if(ok) error_.clear(); return ok;
}
void Store::cleanup(int64_t now) { execute("SELECT cnc_cleanup($1)",{now}); }
Json Store::events(int64_t from,int limit) {
  auto r=query("SELECT details_json FROM events WHERE ts_ms>=$1 ORDER BY ts_ms DESC,id DESC LIMIT $2",{from,limit});
  if(!r) throw std::runtime_error("events unavailable");
  std::unique_ptr<PGresult,decltype(&PQclear)> result(r,PQclear); Json a=Json::array();
  for(int i=0;i<PQntuples(r);++i) a.push_back(Json::parse(PQgetvalue(r,i,0)));
  return a;
}
Json Store::orders(int64_t from) {
  auto r=query("SELECT row_to_json(o) FROM (SELECT * FROM orders WHERE status='active' OR completed_ms>=$1 ORDER BY order_id DESC LIMIT 5000) o",{from});
  if(!r) throw std::runtime_error("orders unavailable");
  std::unique_ptr<PGresult,decltype(&PQclear)> result(r,PQclear); Json a=Json::array();
  for(int i=0;i<PQntuples(r);++i) a.push_back(Json::parse(PQgetvalue(r,i,0)));
  return a;
}
std::function<std::string()> Store::history_stream(int64_t from,int64_t to,int limit,int step) {
  // Dedicated read-only cursor: each fetch retains at most 16 snapshots in libpq.
  auto c=std::shared_ptr<PGconn>(open(connection_,password_),PQfinish);
  if(PQstatus(c.get())!=CONNECTION_OK) throw std::runtime_error("history connection unavailable");
  auto command=[&](const std::string &sql) {
    auto r=PQexec(c.get(),sql.c_str()); bool ok=r && PQresultStatus(r)==PGRES_COMMAND_OK; PQclear(r);
    if(!ok) throw std::runtime_error("history cursor unavailable");
  };
  command("BEGIN READ ONLY");
  command("DECLARE history NO SCROLL CURSOR FOR SELECT payload_json FROM (SELECT DISTINCT ON (ts_ms/"+std::to_string(std::max(1,step)*int64_t(1000))+") ts_ms,payload_json FROM telemetry WHERE ts_ms>="+std::to_string(from)+" AND ts_ms<="+std::to_string(to)+" ORDER BY ts_ms/"+std::to_string(std::max(1,step)*int64_t(1000))+",ts_ms) t ORDER BY ts_ms LIMIT "+std::to_string(std::max(0,std::min(limit,5000))));
  return [c,first=true,done=false]() mutable -> std::string {
    if(done) return {};
    std::unique_ptr<PGresult,decltype(&PQclear)> r(PQexec(c.get(),"FETCH FORWARD 16 FROM history"),PQclear);
    if(!r || PQresultStatus(r.get())!=PGRES_TUPLES_OK) throw std::runtime_error("history read failed");
    std::string chunk=first?"[":"";
    for(int i=0;i<PQntuples(r.get());++i) { if(!first) chunk+=","; chunk+=PQgetvalue(r.get(),i,0); first=false; }
    if(PQntuples(r.get())<16) {chunk+="]";done=true;auto end=PQexec(c.get(),"COMMIT");PQclear(end);}
    return chunk;
  };
}
Json Store::history(int64_t from,int64_t to,int limit,int step) {
  auto stream=history_stream(from,to,limit,step);std::string body;
  for(;;){auto c=stream();if(c.empty())break;body+=c;}return Json::parse(body);
}
}
