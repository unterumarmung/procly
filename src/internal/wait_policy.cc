#include "procly/internal/wait_policy.hpp"

#include <cerrno>

namespace procly::internal {

namespace {

bool is_esrch_error(const Error& error) {
  return error.code.category() == std::system_category() && error.code.value() == ESRCH;
}

Result<ExitStatus> reconcile_after_missed_signal(WaitOps& ops, const Error& error) {
  if (!is_esrch_error(error)) {
    return error;
  }

  auto try_wait_result = ops.try_wait();
  if (!try_wait_result) {
    return try_wait_result.error();
  }
  const std::optional<ExitStatus>& maybe_status = try_wait_result.value();
  if (maybe_status.has_value()) {
    return *maybe_status;
  }
  return ops.wait_blocking();
}

}  // namespace

Result<WaitResult> wait_with_timeout(WaitOps& ops, Clock& clock,
                                     std::optional<std::chrono::milliseconds> timeout,
                                     std::chrono::milliseconds kill_grace) {
  WaitResult result;
  if (!timeout) {
    auto status = ops.wait_blocking();
    if (!status) {
      return status.error();
    }
    result.status = status.value();
    return result;
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
      result.status = *status_opt;
      return result;
    }
    clock.sleep_for(kSleepStep);
  }

  result.timed_out = true;
  auto term_result = ops.terminate();
  if (!term_result) {
    auto status = reconcile_after_missed_signal(ops, term_result.error());
    if (!status) {
      return status.error();
    }
    result.status = status.value();
    return result;
  }
  result.sent_terminate = true;

  auto grace_deadline = clock.now() + kill_grace;
  while (clock.now() < grace_deadline) {
    auto wait_result = ops.try_wait();
    if (!wait_result) {
      return wait_result.error();
    }
    auto& status_opt = wait_result.value();
    if (status_opt.has_value()) {
      result.status = *status_opt;
      return result;
    }
    clock.sleep_for(kSleepStep);
  }

  auto kill_result = ops.kill();
  if (!kill_result) {
    auto status = reconcile_after_missed_signal(ops, kill_result.error());
    if (!status) {
      return status.error();
    }
    result.status = status.value();
    return result;
  }
  result.sent_kill = true;

  auto status = ops.wait_blocking();
  if (!status) {
    return status.error();
  }
  result.status = status.value();
  return result;
}

}  // namespace procly::internal
