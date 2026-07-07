#include "connection_status.hpp"

#include <atomic>

namespace {
std::atomic<connection_status::State> g_state{connection_status::State::kDisconnected};
}  // namespace

void connection_status::set(State state) { g_state.store(state, std::memory_order_relaxed); }

connection_status::State connection_status::get() {
  return g_state.load(std::memory_order_relaxed);
}
