#pragma once
#include "config.hpp"
#include "transport/contract.hpp"
#include <vector>
namespace cnc {
struct MachineSnapshot {
  uint64_t sequence = 0;
  int64_t ts_ms = 0;
  Json data;
  Json tools;
  Json metrics;
  Json nc;
};
std::string utc(int64_t ms);
Json envelope(const MachineSnapshot &s, const Json &data);
std::array<std::vector<uint8_t>, 4> encode_s7(const MachineSnapshot &s);
void canonicalize(MachineSnapshot &s);
} // namespace cnc
