#include "procly/internal/wait_policy.hpp"

namespace procly::internal {

Result<ExitStatus> wait_with_timeout(WaitOps& ops, Clock& clock,
                                     std::optional<std::chrono::milliseconds> timeout,
                                     std::chrono::milliseconds kill_grace) {
  if (!timeout) {
    return ops.wait_blocking();
  }

  constexpr auto kSleepStep = std::chrono::milliseconds(1);
  auto deadline = clock.now() + *timeout;
  while (clock.now() < deadline) {
    auto wait_result = ops.try_wait();
    if (!wait_result) {
      return wait_result.error();
    }
    auto& status_opt = wait_result.value();
    if (status_opt.has_value()) {
      return *status_opt;
    }
    clock.sleep_for(kSleepStep);
  }

  auto term_result = ops.terminate();
  if (!term_result) {
    return term_result.error();
  }

  auto grace_deadline = clock.now() + kill_grace;
  while (clock.now() < grace_deadline) {
    auto wait_result = ops.try_wait();
    if (!wait_result) {
      return wait_result.error();
    }
    auto& status_opt = wait_result.value();
    if (status_opt.has_value()) {
      return Error{make_error_code(errc::timeout), "timeout"};
    }
    clock.sleep_for(kSleepStep);
  }

  auto kill_result = ops.kill();
  if (!kill_result) {
    return kill_result.error();
  }

  (void)ops.wait_blocking();
  return Error{make_error_code(errc::timeout), "timeout"};
}

}  // namespace procly::internal
