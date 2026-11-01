#pragma once
#include "transport/snapshot.hpp"
#include <atomic>
#include <memory>
#include <mutex>
namespace cnc {
class Protocols {
  struct Impl;
  std::unique_ptr<Impl> p_;

public:
  Protocols(const MachineSnapshot &initial);
  ~Protocols();
  void publish(const MachineSnapshot &, const Json &events);
  void pump();
  Json health() const;
};
} // namespace cnc
