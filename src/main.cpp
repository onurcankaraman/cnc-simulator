#include "adapters/http_server.hpp"
#include "adapters/protocols.hpp"
#include "adapters/sqlite_store.hpp"
#include "core/engine.hpp"
#include <csignal>
#include <ctime>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>
using namespace cnc;
static volatile std::sig_atomic_t running = 1;
static int64_t wall_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
static std::string file(const std::string &p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), {});
}
static int number(const Request &r, const char *k, int fallback, int lo, int hi) {
  auto it = r.query.find(k);
  if (it == r.query.end())
    return fallback;
  auto s = it->second;
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    throw std::invalid_argument("invalid numeric query");
  auto n = std::stoll(s);
  if (n < lo || n > hi)
    throw std::invalid_argument("query out of bounds");
  return n;
}
static int64_t timestamp(const std::string &s) {
  if (!std::regex_match(
          s, std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\\.[0-9]{3})?Z")))
    throw std::invalid_argument("UTC ISO8601 required");
  tm t{};
  if (!strptime(s.c_str(), "%Y-%m-%dT%H:%M:%S", &t))
    throw std::invalid_argument("invalid UTC time");
  int64_t ms = int64_t(timegm(&t)) * 1000 + (s.size() == 24 ? std::stoi(s.substr(20, 3)) : 0);
  if (utc(ms).substr(0, 19) != s.substr(0, 19))
    throw std::invalid_argument("invalid calendar date");
  return ms;
}
static bool equal_token(const std::string &a, const std::string &b) {
  size_t diff = a.size() ^ b.size();
  for (size_t i = 0; i < b.size(); i++)
    diff |=
        (i < a.size() ? static_cast<unsigned char>(a[i]) : 0) ^ static_cast<unsigned char>(b[i]);
  return !diff;
}
int main() {
  try {
    signal(SIGTERM, [](int) { running = 0; });
    signal(SIGINT, [](int) { running = 0; });
    signal(SIGPIPE, SIG_IGN);
    auto config = load_config(env("CONFIG_DIR", "/app/config"));
    auto seed = config["plant"]["simulation"]["seed"];
    Engine engine(config, seed.is_null() ? std::random_device{}() : seed.get<uint64_t>());
    Store store(env("DATABASE_PATH", "/data/cnc.sqlite"));
    auto saved = store.restore();
    if (!saved.is_null())
      engine.restore(saved, wall_ms());
    engine.tick(.2, wall_ms());
    auto snapshot = engine.snapshot();
    Protocols protocols(snapshot);
    bool admin = env("ENABLE_ADMIN", "false") == "true";
    auto token = file(env("ADMIN_TOKEN_FILE"));
    while (!token.empty() && isspace(token.back()))
      token.pop_back();
    if (admin && token.size() < 32)
      throw std::runtime_error("admin requires at least 32 random token characters");
    auto last_admin = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    uint64_t persistence_failures = 0;
    bool had_gap = false;
    auto health = [&]() {
      auto h = protocols.health();
      h["database"] = store.healthy();
      h["database_error"] = store.error();
      h["persistence_failures"] = persistence_failures;
      return h;
    };
    Http http(8080, [&](const Request &r) -> Response {
      auto json = [](Json j, int status = 200) { return Response{status, j.dump()}; };
      if (r.path.rfind("/admin/", 0) == 0) {
        if (!admin)
          return json({{"error", "admin disabled"}}, 404);
        auto a = r.headers.find("authorization");
        if (a == r.headers.end() || !equal_token(a->second, "Bearer " + token))
          return json({{"error", "unauthorized"}}, 401);
        auto n = std::chrono::steady_clock::now();
        if (n - last_admin < std::chrono::seconds(1))
          return json({{"error", "rate limited"}}, 429);
        last_admin = n;
        if (r.method != "POST")
          return json({{"error", "POST required"}}, 405);
        auto body = Json::parse(r.body);
        if (!body.is_object())
          throw std::invalid_argument("JSON object required");
        engine.command(r.path.substr(7), body);
        engine.audit(r.path.substr(7));
        return json({{"accepted", true}});
      }
      if (r.method != "GET")
        return json({{"error", "GET required"}}, 405);
      if (r.path == "/health/live")
        return json({{"live", true}});
      if (r.path == "/health/ready") {
        auto h = health();
        return json(h, store.healthy() && h["mqtt"].get<bool>() ? 200 : 503);
      }
      if (r.path == "/api/v1/about")
        return json({{"name", "CNC Simulator"},
                     {"version", "1.0"},
                     {"data_origin", "Synthetic"},
                     {"s7", "Virtual classic S7 DB server; not S7-1200 firmware"},
                     {"admin_enabled", admin},
                     {"namespace_uri", "urn:digitheta:cnc-simulator:machine:v1"}});
      if (r.path == "/api/v1/status") {
        auto j = envelope(snapshot, snapshot.data);
        j["health"] = health();
        j["metrics"] = snapshot.metrics;
        return json(j);
      }
      if (r.path == "/api/v1/tools")
        return json(envelope(snapshot, {{"tools", snapshot.tools}}));
      if (r.path == "/api/v1/metrics") {
        auto j = snapshot.metrics;
        j["health"] = health();
        return json(j);
      }
      if (r.path == "/api/v1/events")
        return json(store.events(wall_ms() - 86400000, number(r, "limit", 100, 1, 5000)));
      if (r.path == "/api/v1/orders")
        return json(store.orders(wall_ms() - 86400000));
      if (r.path == "/api/v1/history") {
        for (auto &kv : r.query)
          if (kv.first != "from" && kv.first != "to" && kv.first != "limit" && kv.first != "step")
            throw std::invalid_argument("unknown query parameter");
        auto now = wall_ms();
        auto from = r.query.count("from") ? timestamp(r.query.at("from")) : now - 86400000,
             to = r.query.count("to") ? timestamp(r.query.at("to")) : now;
        if (from > to)
          throw std::invalid_argument("from exceeds to");
        Response response;
        response.stream = store.history_stream(std::max(from, now - 86400000), std::min(to, now),
                                               number(r, "limit", 1440, 1, 5000),
                                               number(r, "step", 60, 1, 86400));
        return response;
      }
      if (r.path == "/" || r.path == "/app.js" || r.path == "/style.css") {
        auto path = r.path == "/" ? "/index.html" : r.path;
        return {200, file(env("UI_DIR", "/app/ui") + path),
                r.path == "/"         ? "text/html; charset=utf-8"
                : r.path == "/app.js" ? "text/javascript"
                                      : "text/css"};
      }
      return json({{"error", "not found"}}, 404);
    });
    std::cout
        << Json({{"event", "startup"}, {"origin", "Synthetic"}, {"utc", utc(wall_ms())}}).dump()
        << std::endl;
    auto previous = std::chrono::steady_clock::now(), tick = previous, publish = previous,
         sample = previous - std::chrono::seconds(5), clean = previous;
    Json pending = engine.take_events();
    while (running) {
      auto now = std::chrono::steady_clock::now();
      if (now - tick >= std::chrono::milliseconds(200)) {
        double dt = std::chrono::duration<double>(now - tick).count();
        tick = now;
        auto wall = wall_ms();
        while (dt > 1e-9) {
          double slice = std::min(dt, .2);
          engine.tick(slice, wall - int64_t((dt - slice) * 1000));
          dt -= slice;
        }
        auto ev = engine.take_events();
        for (auto &e : ev) {
          if (pending.size() >= 256)
            throw std::runtime_error("event backlog exceeded");
          pending.push_back(e);
        }
        if (now - publish >= std::chrono::seconds(1) || !pending.empty()) {
          publish = now;
          snapshot = engine.snapshot();
          protocols.publish(snapshot, pending);
          bool take = now - sample >= std::chrono::milliseconds(
                                          int(config["plant"]["simulation"]["database_sample_ms"]));
          if (had_gap) {
            pending.push_back({{"event_id", -snapshot.ts_ms},
                               {"ts_ms", snapshot.ts_ms},
                               {"type", "sample_gap"},
                               {"code", 0},
                               {"severity", 2},
                               {"state_from", 0},
                               {"state_to", 0}});
          }
          if (!store.write(snapshot, engine.save(), pending, engine.order(), take)) {
            persistence_failures++;
            if (!had_gap)
              std::cerr << Json({{"event", "database_failure"}, {"error", store.error()}}).dump()
                        << std::endl;
            had_gap = true;
          } else {
            had_gap = false;
            if (take)
              sample = now;
          }
          pending = Json::array();
        }
        if (now - clean >= std::chrono::seconds(60)) {
          store.cleanup(wall_ms());
          clean = now;
        }
      }
      protocols.pump();
      http.pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    snapshot = engine.snapshot();
    store.write(snapshot, engine.save(), engine.take_events(), engine.order(), true);
    std::cout << "{\"event\":\"shutdown\"}" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << Json({{"event", "fatal"}, {"error", e.what()}}).dump() << std::endl;
    return 1;
  }
}
