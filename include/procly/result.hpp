#pragma once

#include <cstdint>
#include <string>
#include <system_error>
#include <type_traits>

#include "procly/platform.hpp"

#if defined(__cpp_lib_expected) && (__cpp_lib_expected >= 202202L)
#include <expected>
#define PROCLY_HAS_STD_EXPECTED 1
#else
#define PROCLY_HAS_STD_EXPECTED 0
#endif

#if !PROCLY_HAS_STD_EXPECTED
#include "procly/internal/expected.hpp"
#endif

namespace procly {

/// @brief Error codes for procly operations.
enum class errc : std::uint8_t {
  /// @brief No error.
  ok = 0,

  // Configuration / API misuse
  /// @brief Command has no argv entries.
  empty_argv,
  /// @brief Invalid stdio configuration.
  invalid_stdio,
  /// @brief Invalid pipeline configuration.
  invalid_pipeline,

  // OS/syscall or API failures
  /// @brief Pipe creation failed.
  pipe_failed,
  /// @brief Process creation failed.
  spawn_failed,
  /// @brief Wait operation failed.
  wait_failed,
  /// @brief Read operation failed.
  read_failed,
  /// @brief Write operation failed.
  write_failed,
  /// @brief File open failed.
  open_failed,
  /// @brief File close failed.
  close_failed,
  /// @brief File descriptor duplication failed.
  dup_failed,
  /// @brief Change-directory failed.
  chdir_failed,
  /// @brief Termination/kill operation failed.
  kill_failed,

  // High-level
  /// @brief Operation timed out.
  timeout,
};

/// @brief Error payload returned by procly APIs.
struct Error {
  /// @brief Error code in the procly category.
  std::error_code code;
  /// @brief Human-readable context for the failure.
  std::string context;
};

/// @brief procly error category for std::error_code.
const std::error_category& error_category() noexcept;
/// @brief Create an error_code in the procly category.
std::error_code make_error_code(errc value) noexcept;

/// @brief Result type used by procly APIs (std::expected-compatible).
#if PROCLY_HAS_STD_EXPECTED
template <typename T>
using Result = std::expected<T, Error>;
#else
template <typename T>
using Result = expected<T, Error>;
#endif

namespace internal {
/// @brief Throw an error as an exception (used by *_or_throw helpers).
[[noreturn]] void throw_error(const Error& error);
}  // namespace internal

}  // namespace procly

namespace std {

/// @brief Enable implicit conversion from procly::errc to std::error_code.
template <>
struct is_error_code_enum<procly::errc> : true_type {};

}  // namespace std
