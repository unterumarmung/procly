#include "procly/internal/clock.hpp"

#include <thread>

namespace procly::internal {

namespace {

class SteadyClock final : public Clock {
 public:
  std::chrono::steady_clock::time_point now() override { return std::chrono::steady_clock::now(); }

  void sleep_for(std::chrono::milliseconds duration) override {
    std::this_thread::sleep_for(duration);
  }
};

thread_local Clock* g_clock_override = nullptr;

}  // namespace

ScopedClockOverride::ScopedClockOverride(Clock& clock) : previous_(g_clock_override) {
  g_clock_override = &clock;
}

ScopedClockOverride::~ScopedClockOverride() { g_clock_override = previous_; }

Clock& default_clock() {
  if (auto* override_clock = g_clock_override) {
    return *override_clock;
  }
  static SteadyClock clock;
  return clock;
}

}  // namespace procly::internal
