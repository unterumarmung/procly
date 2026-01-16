#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include "procly/pipe.hpp"
#include "procly/result.hpp"
#include "procly/status.hpp"

namespace procly {

namespace internal {
/// @brief Internal access helper for Child.
struct ChildAccess;
}  // namespace internal

/// @brief Wait configuration for child processes.
struct WaitOptions {
  /// @brief Default grace period before a forced kill.
  static constexpr std::chrono::milliseconds kDefaultKillGrace{200};
  /// @brief Optional timeout for waiting.
  std::optional<std::chrono::milliseconds> timeout;
  /// @brief Grace period after terminate before kill.
  std::chrono::milliseconds kill_grace{kDefaultKillGrace};
};

/// @brief Running child process handle.
class Child {
 public:
  /// @brief Construct an empty child handle.
  Child() = default;
  /// @brief Move-construct a child handle.
  Child(Child&& other) noexcept;
  /// @brief Move-assign a child handle.
  Child& operator=(Child&& other) noexcept;
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;
  /// @brief Destroy the child handle.
  ~Child();

  /// @brief Process identifier (pid on POSIX).
  [[nodiscard]] int id() const noexcept;

  /// @brief Take ownership of stdin pipe, if present.
  std::optional<PipeWriter> take_stdin() noexcept;
  /// @brief Take ownership of stdout pipe, if present.
  std::optional<PipeReader> take_stdout() noexcept;
  /// @brief Take ownership of stderr pipe, if present.
  std::optional<PipeReader> take_stderr() noexcept;

  /// @brief Wait for child completion.
  Result<ExitStatus> wait();
  /// @brief Non-blocking wait.
  Result<std::optional<ExitStatus>> try_wait();
  /// @brief Wait with timeout and termination policy.
  Result<ExitStatus> wait(WaitOptions options);

  /// @brief Send SIGTERM (or platform equivalent).
  Result<void> terminate();
  /// @brief Send SIGKILL (or platform equivalent).
  Result<void> kill();

  /// @brief Send a POSIX signal (POSIX only).
  Result<void> signal(int signo);

 private:
  /// @brief Opaque platform-specific implementation.
  struct Impl;
  /// @brief Owned implementation state.
  std::unique_ptr<Impl> impl_;

  friend struct internal::ChildAccess;
};

}  // namespace procly
