#pragma once

#include <filesystem>
#include <optional>
#include <variant>

#include "procly/platform.hpp"

#if PROCLY_PLATFORM_POSIX
#include <sys/stat.h>
#endif

namespace procly {

/// @brief File open modes for stdio redirection.
enum class OpenMode {
  /// @brief Read-only.
  read,
  /// @brief Write-only; create and truncate.
  write_truncate,
  /// @brief Write-only; create and append.
  write_append,
  /// @brief Read/write; create if missing.
  read_write,
};

#if PROCLY_PLATFORM_POSIX
/// @brief POSIX file permission bits (mode_t).
using FilePerms = ::mode_t;
#endif

/// @brief File specification for stdio redirection.
struct FileSpec {
  /// @brief Path to the file.
  std::filesystem::path path;
  /// @brief Optional open mode; defaults based on stdio target.
  std::optional<OpenMode> mode;
#if PROCLY_PLATFORM_POSIX
  /// @brief Optional permissions for new files (POSIX).
  std::optional<FilePerms> perms;
#endif
};

/// @brief Stdio configuration for a child process.
struct Stdio {
  /// @brief Inherit from parent.
  struct Inherit {};
  /// @brief Attach to null device.
  struct Null {};
  /// @brief Create a pipe and expose the parent end.
  struct Piped {};
  /// @brief Duplicate an existing file descriptor (POSIX).
  struct Fd {
    /// @brief Native file descriptor to duplicate.
    int fd;
  };
  /// @brief Open a file path for redirection.
  using File = FileSpec;

  /// @brief Variant holding the stdio selection.
  std::variant<Inherit, Null, Piped, Fd, File> value;

  /// @brief Inherit the parent's stream.
  static Stdio inherit() { return Stdio{Inherit{}}; }
  /// @brief Redirect to null.
  static Stdio null() { return Stdio{Null{}}; }
  /// @brief Create a pipe.
  static Stdio piped() { return Stdio{Piped{}}; }
  /// @brief Duplicate a file descriptor (POSIX).
  static Stdio fd(int fd) { return Stdio{Fd{fd}}; }
  /// @brief Redirect to a file path.
  static Stdio file(std::filesystem::path path) { return Stdio{FileSpec{std::move(path)}}; }
  /// @brief Redirect to a file path with an explicit open mode.
  static Stdio file(std::filesystem::path path, OpenMode mode) {
    return Stdio{FileSpec{std::move(path), mode}};
  }
  /// @brief Redirect to a file path with full specification.
  static Stdio file(FileSpec spec) { return Stdio{std::move(spec)}; }
#if PROCLY_PLATFORM_POSIX
  /// @brief Redirect to a file path with explicit mode and permissions (POSIX only).
  static Stdio file(std::filesystem::path path, OpenMode mode, FilePerms perms) {
    return Stdio{FileSpec{std::move(path), mode, perms}};
  }
#endif
};

}  // namespace procly
