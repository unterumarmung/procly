#pragma once

#include <chrono>
#include <functional>
#include <optional>

#include "procly/internal/clock.hpp"
#include "procly/result.hpp"
#include "procly/status.hpp"

namespace procly::internal {

struct WaitOps {
  std::function<Result<std::optional<ExitStatus>>()> try_wait;
  std::function<Result<ExitStatus>()> wait_blocking;
  std::function<Result<void>()> terminate;
  std::function<Result<void>()> kill;
};

Result<ExitStatus> wait_with_timeout(WaitOps& ops, Clock& clock,
                                     std::optional<std::chrono::milliseconds> timeout,
                                     std::chrono::milliseconds kill_grace);

}  // namespace procly::internal
