#include "procly/internal/clock.hpp"

#include <atomic>
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

std::atomic<Clock*> g_clock_override{nullptr};

}  // namespace

ScopedClockOverride::ScopedClockOverride(Clock& clock)
    : previous_(g_clock_override.exchange(&clock)) {}

ScopedClockOverride::~ScopedClockOverride() { g_clock_override.store(previous_); }

Clock& default_clock() {
  if (auto* override_clock = g_clock_override.load()) {
    return *override_clock;
  }
  static SteadyClock clock;
  return clock;
}

}  // namespace procly::internal
