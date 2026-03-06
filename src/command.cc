#include "procly/command.hpp"

#include <utility>

#include "procly/child.hpp"
#include "procly/internal/access.hpp"
#include "procly/internal/backend.hpp"
#include "procly/internal/io_drain.hpp"
#include "procly/internal/lowering.hpp"

namespace procly {

namespace {

Result<internal::Spawned> spawn_command(const Command& command, internal::SpawnMode mode) {
  auto lowered = internal::lower_command(command, mode, nullptr);
  if (!lowered) {
    return lowered.error();
  }

  auto& backend = internal::default_backend();
  auto spawned = backend.spawn(lowered.value());
  if (!spawned) {
    return spawned.error();
  }
  spawned->backend = &backend;
  return spawned.value();
}

}  // namespace

Command::Command(std::string program) { argv_.emplace_back(std::move(program)); }

Command& Command::arg(std::string value) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  argv_.emplace_back(std::move(value));
  return *this;
}

Command& Command::arg(const char* value) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  argv_.emplace_back(value);
  return *this;
}

Command& Command::arg(std::string_view value) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  argv_.emplace_back(value);
  return *this;
}

Command& Command::args(std::initializer_list<std::string_view> values) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  for (auto value : values) {
    argv_.emplace_back(value);
  }
  return *this;
}

Command& Command::args(const std::string* values, std::size_t count) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  for (std::size_t i = 0; i < count; ++i) {
    argv_.emplace_back(values[i]);
  }
  return *this;
}

#if PROCLY_HAS_STD_SPAN
Command& Command::args(std::span<const std::string> values) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  for (const auto& value : values) {
    argv_.emplace_back(value);
  }
  return *this;
}
#endif

Command& Command::current_dir(std::filesystem::path path) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  cwd_ = std::move(path);
  return *this;
}

Command& Command::env(std::string key, std::string value) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  env_delta_[std::move(key)] = std::move(value);
  return *this;
}

Command& Command::env_remove(std::string_view key) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  env_delta_[std::string(key)] = std::nullopt;
  return *this;
}

Command& Command::env_clear() {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  inherit_env_ = false;
  env_delta_.clear();
  return *this;
}

Command& Command::stdin(Stdio value) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  stdin_ = std::move(value);
  return *this;
}

Command& Command::stdout(Stdio value) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  stdout_ = std::move(value);
  return *this;
}

Command& Command::stderr(Stdio value) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  stderr_ = std::move(value);
  return *this;
}

Command& Command::options(SpawnOptions value) {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  opts_ = value;
  return *this;
}

Result<Child> Command::spawn() const {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  auto spawned = spawn_command(*this, internal::SpawnMode::spawn);
  if (!spawned) {
    return spawned.error();
  }
  return internal::ChildAccess::from_spawned(spawned.value());
}

Result<ExitStatus> Command::status() const {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  auto spawned = spawn_command(*this, internal::SpawnMode::spawn);
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
  if (stdout_pipe || stderr_pipe) {
    auto drained = internal::drain_pipes(stdout_pipe ? &*stdout_pipe : nullptr,
                                         stderr_pipe ? &*stderr_pipe : nullptr);
    if (!drained) {
      return drained.error();
    }
  }

  return child.wait();
}

Result<Output> Command::output() const {
  auto use = concurrent_use_.enter("Command");
  (void)use;
  auto spawned = spawn_command(*this, internal::SpawnMode::output);
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
