#include "adapters/sqlite_store.hpp"
#include "core/engine.hpp"
#include <cstdio>
#include <iostream>
#include <sys/resource.h>
using namespace cnc;
int main() {
  std::remove("/tmp/cnc-memory.sqlite");
  Engine e(load_config("config"), 1);
  e.tick(.2, 1793516400000);
  auto s = e.snapshot();
  Store store("/tmp/cnc-memory.sqlite");
  auto state = e.save(), order = e.order();
  for (int i = 0; i < 5000; i++) {
    s.ts_ms += 1000;
    store.write(s, state, Json::array(), order, true);
  }
  size_t bytes = 0, max_chunk = 0;
  auto stream = store.history_stream(s.ts_ms - 5000000, s.ts_ms, 5000, 1);
  for (;;) {
    auto c = stream();
    if (c.empty())
      break;
    bytes += c.size();
    max_chunk = std::max(max_chunk, c.size());
  }
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  std::cout << "stream rows=5000 bytes=" << bytes << " max_chunk=" << max_chunk
            << " peak_rss_kib=" << usage.ru_maxrss << "\n";
  return usage.ru_maxrss > 40000 || max_chunk > 65536 ? 1 : 0;
}
