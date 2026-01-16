#include "procly/status.hpp"

namespace procly {

ExitStatus ExitStatus::exited(
    int code, std::uint32_t native) noexcept {  // NOLINT(bugprone-easily-swappable-parameters)
  ExitStatus status;
  status.kind_ = Kind::exited;
  status.code_ = code;
  status.native_ = native;
  return status;
}

ExitStatus ExitStatus::other(std::uint32_t native) noexcept {
  ExitStatus status;
  status.kind_ = Kind::other;
  status.native_ = native;
  return status;
}

std::optional<int> ExitStatus::code() const noexcept {
  if (kind_ != Kind::exited) {
    return std::nullopt;
  }
  return code_;
}

}  // namespace procly
