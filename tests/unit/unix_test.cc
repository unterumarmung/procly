#include "procly/unix.hpp"

#include <gtest/gtest.h>

#include <csignal>

#include "procly/platform.hpp"

namespace procly {

TEST(UnixTest, RawWaitStatusRoundTrip) {
  ExitStatus status = ExitStatus::other(123);
  auto raw = procly::unix::raw_wait_status(status);
  ASSERT_TRUE(raw.has_value());
  EXPECT_EQ(raw.value(), 123);
}

#if PROCLY_PLATFORM_POSIX
TEST(UnixTest, TerminatingSignalExtractsSignal) {
  int raw_status = SIGTERM;
  ExitStatus status = ExitStatus::other(static_cast<std::uint32_t>(raw_status));
  auto signal = procly::unix::terminating_signal(status);
  ASSERT_TRUE(signal.has_value());
  EXPECT_EQ(signal.value(), SIGTERM);
}
#endif

}  // namespace procly
