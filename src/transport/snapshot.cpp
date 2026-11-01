#include "transport/snapshot.hpp"
#include <cmath>
#include <cstring>
#include <ctime>
#include <stdexcept>
namespace cnc {
std::string utc(int64_t ms) {
  time_t sec = ms / 1000;
  tm t{};
  gmtime_r(&sec, &t);
  char b[40];
  strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &t);
  char suffix[10];
  snprintf(suffix, sizeof(suffix), ".%03dZ", int(ms % 1000));
  return std::string(b) + suffix;
}
Json envelope(const MachineSnapshot &s, const Json &d) {
  return {{"schema_version", 1},
          {"machine_id", "CNC-01"},
          {"ts_utc", utc(s.ts_ms)},
          {"sequence", s.sequence},
          {"data", d}};
}
void canonicalize(MachineSnapshot &s) {
  for (auto &t : tags) {
    auto &v = s.data[t.key];
    if (t.kind == Kind::STRING32 || t.kind == Kind::STRING48) {
      if (v.is_null())
        v = "";
      continue;
    }
    if (v.is_null())
      v = 0;
    double d = v.get<double>();
    if (!std::isfinite(d) || d < t.low - 0.001 || d > t.high + 0.001)
      throw std::runtime_error(std::string("range: ") + t.key + "=" + std::to_string(d));
    if (t.kind == Kind::REAL32)
      v = float(d);
    else
      v = uint32_t(d);
  }
}
std::array<std::vector<uint8_t>, 4> encode_s7(const MachineSnapshot &s) {
  std::array<std::vector<uint8_t>, 4> b = {std::vector<uint8_t>(256), std::vector<uint8_t>(256),
                                           std::vector<uint8_t>(256), std::vector<uint8_t>(128)};
  for (auto &t : tags) {
    auto p = b[t.db - 1].data() + t.offset;
    auto &v = s.data.at(t.key);
    if (t.kind == Kind::STRING32 || t.kind == Kind::STRING48) {
      auto str = v.get<std::string>();
      p[0] = t.size - 2;
      p[1] = std::min<size_t>(str.size(), p[0]);
      memcpy(p + 2, str.data(), p[1]);
    } else {
      uint32_t u;
      if (t.kind == Kind::REAL32) {
        float f = v.get<float>();
        memcpy(&u, &f, 4);
      } else
        u = v.get<uint32_t>();
      for (int i = 0; i < t.size; i++)
        p[i] = (u >> (8 * (t.size - i - 1))) & 255;
    }
  }
  return b;
}
} // namespace cnc
