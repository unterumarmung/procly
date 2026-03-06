#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <optional>
#include <vector>

#include "procly/internal/wait_policy.hpp"

namespace procly {
namespace {

class FakeClock final : public internal::Clock {
 public:
  std::chrono::steady_clock::time_point now() override { return now_; }

  void sleep_for(std::chrono::milliseconds duration) override {
    sleep_calls.push_back(duration);
    now_ += duration;
  }

  std::chrono::milliseconds elapsed() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(now_.time_since_epoch());
  }

  std::vector<std::chrono::milliseconds> sleep_calls;

 private:
  std::chrono::steady_clock::time_point now_{};
};

struct TestOps {
  Result<std::optional<ExitStatus>> try_wait() {
    ++try_wait_calls;
    if (immediate_exit) {
      return std::optional<ExitStatus>(ExitStatus::exited(0));
    }
    if (exit_after_terminate && terminated) {
      return std::optional<ExitStatus>(ExitStatus::exited(0));
    }
    return std::optional<ExitStatus>();
  }

  Result<ExitStatus> wait_blocking() {
    ++wait_calls;
    return ExitStatus::exited(0);
  }

  Result<void> terminate() {
    ++terminate_calls;
    terminated = true;
    return {};
  }

  Result<void> kill() {
    ++kill_calls;
    killed = true;
    return {};
  }

  int try_wait_calls = 0;
  int terminate_calls = 0;
  int kill_calls = 0;
  int wait_calls = 0;
  bool terminated = false;
  bool killed = false;
  bool immediate_exit = false;
  bool exit_after_terminate = false;
  bool terminate_returns_esrch = false;
  bool kill_returns_esrch = false;
};

}  // namespace

TEST(TimeoutPolicyTest, ReturnsStatusBeforeTimeout) {
  FakeClock clock;
  TestOps ops_impl;
  ops_impl.immediate_exit = true;

  internal::WaitOps ops;
  ops.try_wait = [&]() { return ops_impl.try_wait(); };
  ops.wait_blocking = [&]() { return ops_impl.wait_blocking(); };
  ops.terminate = [&]() { return ops_impl.terminate(); };
  ops.kill = [&]() { return ops_impl.kill(); };

  auto result = internal::wait_with_timeout(ops, clock, std::chrono::milliseconds(5),
                                            std::chrono::milliseconds(5));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->success());
  EXPECT_FALSE(result->timed_out);
  EXPECT_FALSE(result->sent_terminate);
  EXPECT_FALSE(result->sent_kill);
  EXPECT_EQ(ops_impl.terminate_calls, 0);
  EXPECT_EQ(ops_impl.kill_calls, 0);
  EXPECT_EQ(ops_impl.wait_calls, 0);
}

TEST(TimeoutPolicyTest, ClockOverrideRestoresDefault) {
  FakeClock clock;
  internal::Clock* before = &internal::default_clock();
  {
    internal::ScopedClockOverride override_clock(clock);
    EXPECT_EQ(&internal::default_clock(), &clock);
  }
  EXPECT_EQ(&internal::default_clock(), before);
}

TEST(TimeoutPolicyTest, TimeoutTriggersTerminateDuringGrace) {
  FakeClock clock;
  TestOps ops_impl;
  ops_impl.exit_after_terminate = true;

  internal::WaitOps ops;
  ops.try_wait = [&]() { return ops_impl.try_wait(); };
  ops.wait_blocking = [&]() { return ops_impl.wait_blocking(); };
  ops.terminate = [&]() { return ops_impl.terminate(); };
  ops.kill = [&]() { return ops_impl.kill(); };

  auto result = internal::wait_with_timeout(ops, clock, std::chrono::milliseconds(3),
                                            std::chrono::milliseconds(5));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->timed_out);
  EXPECT_TRUE(result->sent_terminate);
  EXPECT_FALSE(result->sent_kill);
  EXPECT_TRUE(result->success());
  EXPECT_EQ(ops_impl.terminate_calls, 1);
  EXPECT_EQ(ops_impl.kill_calls, 0);
  EXPECT_EQ(ops_impl.wait_calls, 0);
  EXPECT_GE(clock.elapsed(), std::chrono::milliseconds(3));
}

TEST(TimeoutPolicyTest, TimeoutEscalatesToKill) {
  FakeClock clock;
  TestOps ops_impl;

  internal::WaitOps ops;
  ops.try_wait = [&]() { return ops_impl.try_wait(); };
  ops.wait_blocking = [&]() { return ops_impl.wait_blocking(); };
  ops.terminate = [&]() { return ops_impl.terminate(); };
  ops.kill = [&]() { return ops_impl.kill(); };

  auto result = internal::wait_with_timeout(ops, clock, std::chrono::milliseconds(3),
                                            std::chrono::milliseconds(4));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->timed_out);
  EXPECT_TRUE(result->sent_terminate);
  EXPECT_TRUE(result->sent_kill);
  EXPECT_TRUE(result->success());
  EXPECT_EQ(ops_impl.terminate_calls, 1);
  EXPECT_EQ(ops_impl.kill_calls, 1);
  EXPECT_EQ(ops_impl.wait_calls, 1);
  EXPECT_GE(clock.elapsed(), std::chrono::milliseconds(7));
}

TEST(TimeoutPolicyTest, TreatsEsrchDuringTerminateAsExitedProcess) {
  FakeClock clock;
  TestOps ops_impl;
  ops_impl.terminate_returns_esrch = true;

  internal::WaitOps ops;
  ops.try_wait = [&]() { return ops_impl.try_wait(); };
  ops.wait_blocking = [&]() { return ops_impl.wait_blocking(); };
  ops.terminate = [&]() -> Result<void> {
    ++ops_impl.terminate_calls;
    return Error{.code = std::error_code(ESRCH, std::system_category()), .context = "kill"};
  };
  ops.kill = [&]() { return ops_impl.kill(); };

  auto result = internal::wait_with_timeout(ops, clock, std::chrono::milliseconds(1),
                                            std::chrono::milliseconds(1));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->timed_out);
  EXPECT_FALSE(result->sent_terminate);
  EXPECT_FALSE(result->sent_kill);
  EXPECT_TRUE(result->success());
  EXPECT_EQ(ops_impl.wait_calls, 1);
}

}  // namespace procly
