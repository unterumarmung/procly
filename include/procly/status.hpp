#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace procly {

/// @brief Portable process exit status.
class ExitStatus {
 public:
  /// @brief The kind of exit status.
  enum class Kind : std::uint8_t {
    /// @brief Process exited normally with an exit code.
    exited,
    /// @brief Process ended due to signal or other non-exit condition.
    other
  };

  /// @brief Construct a normal exit status with an exit code.
  static ExitStatus exited(int code, std::uint32_t native = 0) noexcept;
  /// @brief Construct a non-exited status (signal/other).
  static ExitStatus other(std::uint32_t native = 0) noexcept;

  /// @brief Kind discriminator.
  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  /// @brief True if exited with code 0.
  [[nodiscard]] bool success() const noexcept { return kind_ == Kind::exited && code_ == 0; }
  /// @brief Exit code if available.
  [[nodiscard]] std::optional<int> code() const noexcept;
  /// @brief Native OS status (wait status or exit code).
  [[nodiscard]] std::uint32_t native() const noexcept { return native_; }

 private:
  Kind kind_{Kind::other};
  int code_{0};
  std::uint32_t native_{0};
};

/// @brief Captured output from a process.
struct Output {
  /// @brief Exit status for the process.
  ExitStatus status;
  /// @brief Captured stdout data.
  std::string stdout_data;
  /// @brief Captured stderr data.
  std::string stderr_data;
};

}  // namespace procly
