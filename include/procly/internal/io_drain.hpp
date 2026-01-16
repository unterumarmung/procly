#pragma once

#include <optional>
#include <string>

#include "procly/pipe.hpp"
#include "procly/result.hpp"

namespace procly::internal {

struct DrainResult {
  std::string stdout_data;
  std::string stderr_data;
};

Result<DrainResult> drain_pipes(PipeReader* stdout_pipe, PipeReader* stderr_pipe);

}  // namespace procly::internal
