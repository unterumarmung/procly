#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "procly/child.hpp"
#include "procly/platform.hpp"
#include "procly/result.hpp"
#include "procly/status.hpp"
#include "procly/stdio.hpp"

#if PROCLY_HAS_STD_SPAN
#include <span>
#endif

namespace procly {

namespace internal {
/// @brief Internal access helper for Command.
struct CommandAccess;
}  // namespace internal

/// @brief Options that affect process creation.
struct SpawnOptions {
  /// @brief Create a new process group.
  bool new_process_group = false;
  /// @brief Merge stderr into stdout.
  bool merge_stderr_into_stdout = false;
};

/// @brief Builder for launching a child process.
class Command {
 public:
  /// @brief Construct a command with argv[0]=program.
  explicit Command(std::string program);

  /// @brief Append a single argument (owned string).
  Command& arg(std::string value);
  /// @brief Append a single argument from a C string.
  Command& arg(const char* value);
  /// @brief Append a single argument from a string view.
  Command& arg(std::string_view value);
  /// @brief Append multiple arguments from an initializer list.
  Command& args(std::initializer_list<std::string_view> values);
  /// @brief Append multiple arguments from an array.
  Command& args(const std::string* values, std::size_t count);
#if PROCLY_HAS_STD_SPAN
  /// @brief Append multiple arguments from a span.
  Command& args(std::span<const std::string> values);
#endif

  /// @brief Set current working directory for the child.
  Command& current_dir(std::filesystem::path path);

  /// @brief Set or override an environment variable.
  Command& env(std::string key, std::string value);
  /// @brief Remove an environment variable.
  Command& env_remove(std::string_view key);
  /// @brief Clear inherited environment.
  Command& env_clear();

  /// @brief Configure stdin.
  Command& stdin(Stdio value);
  /// @brief Configure stdout.
  Command& stdout(Stdio value);
  /// @brief Configure stderr.
  Command& stderr(Stdio value);

  /// @brief Set spawn options.
  Command& options(SpawnOptions value);

  /// @brief Spawn without waiting.
  [[nodiscard]] Result<Child> spawn() const;
  /// @brief Spawn and wait for exit status.
  [[nodiscard]] Result<ExitStatus> status() const;
  /// @brief Spawn, capture output, and wait.
  [[nodiscard]] Result<Output> output() const;

  /// @brief Spawn and throw on error.
  [[nodiscard]] Child spawn_or_throw() const;
  /// @brief Wait and throw on error.
  [[nodiscard]] ExitStatus status_or_throw() const;
  /// @brief Capture output and throw on error.
  [[nodiscard]] Output output_or_throw() const;

 private:
  /// @brief Argument vector (argv[0] is the program).
  std::vector<std::string> argv_;
  /// @brief Optional working directory for the child.
  std::optional<std::filesystem::path> cwd_;

  /// @brief Whether to inherit the parent environment.
  bool inherit_env_ = true;
  /// @brief Environment updates (set/unset) to apply to the child.
  std::map<std::string, std::optional<std::string>, std::less<>> env_delta_;

  /// @brief Optional stdin configuration override.
  std::optional<Stdio> stdin_;
  /// @brief Optional stdout configuration override.
  std::optional<Stdio> stdout_;
  /// @brief Optional stderr configuration override.
  std::optional<Stdio> stderr_;
  /// @brief Spawn options for the command.
  SpawnOptions opts_{};

  friend struct internal::CommandAccess;
};

}  // namespace procly
