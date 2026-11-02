#include "config.hpp"
#include "core/nc_program.hpp"
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
namespace cnc {
std::string env(const char *n, const std::string &f) {
  auto p = getenv(n);
  return p ? p : f;
}
static Json convert(const YAML::Node &n) {
  if (n.IsNull())
    return nullptr;
  if (n.IsSequence()) {
    Json a = Json::array();
    for (auto v : n)
      a.push_back(convert(v));
    return a;
  }
  if (n.IsMap()) {
    Json o = Json::object();
    for (auto v : n)
      o[v.first.as<std::string>()] = convert(v.second);
    return o;
  }
  std::string s = n.Scalar();
  if (s == "true")
    return true;
  if (s == "false")
    return false;
  char *e = nullptr;
  double d = strtod(s.c_str(), &e);
  if (e != s.c_str() && !*e) {
    if (s.find_first_of(".eE") == std::string::npos)
      return int64_t(d);
    return d;
  }
  return s;
}
Json load_config(const std::string &dir) {
  Json c;
  for (auto name : {"plant", "tools", "programs", "faults"})
    c[name] = convert(YAML::LoadFile(dir + "/" + name + ".yaml"));
  if (c["tools"]["tools"].size() != 12 || c["programs"]["programs"].size() != 4)
    throw std::runtime_error("12 tools and 4 programs required");
  int slot = 1;
  for (auto &t : c["tools"]["tools"]) {
    if (int(t.at("slot")) != slot++ || double(t.at("nominal_life_min")) <= 0)
      throw std::runtime_error("invalid tool");
  }
  for (auto &p : c["programs"]["programs"]) {
    double sum = 0;
    for (auto &s : p["segments"]) {
      double d = s.at("duration_s");
      if (d <= 0 || double(s.at("rpm")) > 12000 || s.at("target_xyz_mm").size() != 3)
        throw std::runtime_error("invalid segment");
      std::set<std::string> steps = {"LOAD",  "RAPID",     "CUT",    "DRILL",
                                     "DWELL", "TOOL_SWAP", "UNLOAD", "INSPECT"};
      if (!steps.count(s.at("step").get<std::string>()) || s.at("tool_id").get<int>() < 0 ||
          s.at("tool_id").get<int>() > 12 || double(s.at("rpm")) < 0 ||
          double(s.at("feed_mm_min")) < 0 || double(s.at("feed_mm_min")) > 15000 ||
          !s.at("coolant").is_boolean())
        throw std::runtime_error("invalid NC operation");
      for (int axis = 0; axis < 3; axis++)
        if (double(s["target_xyz_mm"][axis]) < 0 ||
            double(s["target_xyz_mm"][axis]) > (axis == 0 ? 800 : 500))
          throw std::runtime_error("axis target out of bounds");
      sum += d;
    }
    if (p["segments"].back()["step"] != "INSPECT")
      throw std::runtime_error("program must finish at inspection");
    compile_nc_program(p);
    p["nominal_cycle_seconds"] = sum;
  }
  if (double(c["plant"]["simulation"]["speed"]) != 1)
    throw std::runtime_error("production speed must be 1");
  return c;
}
} // namespace cnc
