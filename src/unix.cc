#include "procly/unix.hpp"

#include <sys/wait.h>

namespace procly::unix {

std::optional<int> terminating_signal(const procly::ExitStatus& status) noexcept {
  int raw = static_cast<int>(status.native());
  if (WIFSIGNALED(raw)) {
    return WTERMSIG(raw);
  }
  return std::nullopt;
}

std::optional<int> raw_wait_status(const procly::ExitStatus& status) noexcept {
  return static_cast<int>(status.native());
}

}  // namespace procly::unix
