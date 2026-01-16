#pragma once

#include <optional>

#include "procly/status.hpp"

namespace procly::unix {

/// @brief Extract terminating signal from a POSIX wait status, if present.
std::optional<int> terminating_signal(const procly::ExitStatus& status) noexcept;
/// @brief Access raw POSIX wait status.
std::optional<int> raw_wait_status(const procly::ExitStatus& status) noexcept;

}  // namespace procly::unix
