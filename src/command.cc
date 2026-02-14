#include "procly/command.hpp"

#include <utility>

#include "procly/child.hpp"
#include "procly/internal/access.hpp"
#include "procly/internal/backend.hpp"
#include "procly/internal/io_drain.hpp"
#include "procly/internal/lowering.hpp"

namespace procly {

Command::Command(std::string program) { argv_.emplace_back(std::move(program)); }

Command& Command::arg(std::string value) {
  argv_.emplace_back(std::move(value));
  return *this;
}

Command& Command::arg(const char* value) {
  argv_.emplace_back(value);
  return *this;
}

Command& Command::arg(std::string_view value) {
  argv_.emplace_back(value);
  return *this;
}

Command& Command::args(std::initializer_list<std::string_view> values) {
  for (auto value : values) {
    argv_.emplace_back(value);
  }
  return *this;
}

Command& Command::args(const std::string* values, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    argv_.emplace_back(values[i]);
  }
  return *this;
}

#if PROCLY_HAS_STD_SPAN
Command& Command::args(std::span<const std::string> values) {
  for (const auto& value : values) {
    argv_.emplace_back(value);
  }
  return *this;
}
#endif

Command& Command::current_dir(std::filesystem::path path) {
  cwd_ = std::move(path);
  return *this;
}

Command& Command::env(std::string key, std::string value) {
  env_delta_[std::move(key)] = std::move(value);
  return *this;
}

Command& Command::env_remove(std::string_view key) {
  env_delta_[std::string(key)] = std::nullopt;
  return *this;
}

Command& Command::env_clear() {
  inherit_env_ = false;
  return *this;
}

Command& Command::stdin(Stdio value) {
  stdin_ = std::move(value);
  return *this;
}

Command& Command::stdout(Stdio value) {
  stdout_ = std::move(value);
  return *this;
}

Command& Command::stderr(Stdio value) {
  stderr_ = std::move(value);
  return *this;
}

Command& Command::options(SpawnOptions value) {
  opts_ = value;
  return *this;
}

Result<Child> Command::spawn() const {
  auto lowered = internal::lower_command(*this, internal::SpawnMode::spawn, nullptr);
  if (!lowered) {
    return lowered.error();
  }

  auto& backend = internal::default_backend();
  auto spawned = backend.spawn(lowered.value());
  if (!spawned) {
    return spawned.error();
  }

  return internal::ChildAccess::from_spawned(spawned.value());
}

Result<ExitStatus> Command::status() const {
  auto child_result = spawn();
  if (!child_result) {
    return child_result.error();
  }

  auto stdin_pipe = child_result->take_stdin();
  if (stdin_pipe) {
    stdin_pipe->close();
  }

  auto stdout_pipe = child_result->take_stdout();
  auto stderr_pipe = child_result->take_stderr();
  if (stdout_pipe || stderr_pipe) {
    auto drained = internal::drain_pipes(stdout_pipe ? &*stdout_pipe : nullptr,
                                         stderr_pipe ? &*stderr_pipe : nullptr);
    if (!drained) {
      return drained.error();
    }
  }

  return child_result->wait();
}

Result<Output> Command::output() const {
  auto lowered = internal::lower_command(*this, internal::SpawnMode::output, nullptr);
  if (!lowered) {
    return lowered.error();
  }
  auto& backend = internal::default_backend();
  auto spawned = backend.spawn(lowered.value());
  if (!spawned) {
    return spawned.error();
  }

  Child child = internal::ChildAccess::from_spawned(spawned.value());

  auto stdin_pipe = child.take_stdin();
  if (stdin_pipe) {
    stdin_pipe->close();
  }

  auto stdout_pipe = child.take_stdout();
  auto stderr_pipe = child.take_stderr();

  auto drained = internal::drain_pipes(stdout_pipe ? &*stdout_pipe : nullptr,
                                       stderr_pipe ? &*stderr_pipe : nullptr);
  if (!drained) {
    return drained.error();
  }

  auto status = child.wait();
  if (!status) {
    return status.error();
  }

  Output output;
  output.status = status.value();
  output.stdout_data = std::move(drained->stdout_data);
  output.stderr_data = std::move(drained->stderr_data);
  return output;
}

Child Command::spawn_or_throw() const {
  auto result = spawn();
  if (!result) {
    internal::throw_error(result.error());
  }
  return std::move(result.value());
}

ExitStatus Command::status_or_throw() const {
  auto result = status();
  if (!result) {
    internal::throw_error(result.error());
  }
  return result.value();
}

Output Command::output_or_throw() const {
  auto result = output();
  if (!result) {
    internal::throw_error(result.error());
  }
  return std::move(result.value());
}

}  // namespace procly
