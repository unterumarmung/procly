#pragma once

#include <chrono>

namespace procly::internal {

class Clock {
 public:
  virtual ~Clock() = default;
  virtual std::chrono::steady_clock::time_point now() = 0;
  virtual void sleep_for(std::chrono::milliseconds duration) = 0;
};

class ScopedClockOverride {
 public:
  explicit ScopedClockOverride(Clock& clock);
  ~ScopedClockOverride();
  ScopedClockOverride(const ScopedClockOverride&) = delete;
  ScopedClockOverride& operator=(const ScopedClockOverride&) = delete;

 private:
  Clock* previous_ = nullptr;
};

Clock& default_clock();

}  // namespace procly::internal
