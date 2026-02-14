#include "procly/internal/lowering.hpp"

#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "procly/internal/access.hpp"

#if PROCLY_PLATFORM_MACOS
#include <crt_externs.h>
#endif

namespace procly::internal {

namespace {

char** process_environ() {
#if PROCLY_PLATFORM_MACOS
  char*** envp = _NSGetEnviron();
  return (envp != nullptr) ? *envp : nullptr;
#else
  return ::environ;
#endif
}

enum class StdioTarget : std::uint8_t { stdin, stdout, stderr };

OpenMode default_open_mode(StdioTarget target) {
  return target == StdioTarget::stdin ? OpenMode::read : OpenMode::write_truncate;
}

bool mode_is_readable(OpenMode mode) {
  return mode == OpenMode::read || mode == OpenMode::read_write;
}

bool mode_is_writable(OpenMode mode) {
  return mode == OpenMode::write_truncate || mode == OpenMode::write_append ||
         mode == OpenMode::read_write;
}

Result<StdioSpec> resolve_stdio(const std::optional<Stdio>& value, bool piped_default,
                                StdioTarget target) {
  StdioSpec spec;
  if (!value) {
    spec.kind = piped_default ? StdioSpec::Kind::piped : StdioSpec::Kind::inherit;
    return spec;
  }

  if (std::holds_alternative<Stdio::Inherit>(value->value)) {
    spec.kind = StdioSpec::Kind::inherit;
  } else if (std::holds_alternative<Stdio::Null>(value->value)) {
    spec.kind = StdioSpec::Kind::null;
  } else if (std::holds_alternative<Stdio::Piped>(value->value)) {
    spec.kind = StdioSpec::Kind::piped;
  } else if (std::holds_alternative<Stdio::Fd>(value->value)) {
    int fd = std::get<Stdio::Fd>(value->value).fd;
    if (fd < 0) {
      return Error{.code = make_error_code(errc::invalid_stdio), .context = "fd"};
    }
    spec.kind = StdioSpec::Kind::fd;
    spec.fd = fd;
  } else if (std::holds_alternative<Stdio::File>(value->value)) {
    const auto& file = std::get<Stdio::File>(value->value);
    OpenMode mode = file.mode.value_or(default_open_mode(target));
    if (target == StdioTarget::stdin) {
      if (!mode_is_readable(mode)) {
        return Error{.code = make_error_code(errc::invalid_stdio), .context = "file_mode"};
      }
    } else {
      if (!mode_is_writable(mode)) {
        return Error{.code = make_error_code(errc::invalid_stdio), .context = "file_mode"};
      }
    }
    spec.kind = StdioSpec::Kind::file;
    spec.path = file.path;
    spec.mode = mode;
#if PROCLY_PLATFORM_POSIX
    spec.perms = file.perms;
#endif
  }
  return spec;
}

}  // namespace

Result<SpawnSpec> lower_command(const Command& cmd, SpawnMode mode,
                                const StdioOverride* override_stdio) {
  if (CommandAccess::argv(cmd).empty()) {
    return Error{.code = make_error_code(errc::empty_argv), .context = "argv"};
  }

  SpawnSpec spec;
  spec.argv = CommandAccess::argv(cmd);
  spec.cwd = CommandAccess::cwd(cmd);
  spec.opts = CommandAccess::options(cmd);

  std::map<std::string, std::string, std::less<>> env_map;
  if (CommandAccess::inherit_env(cmd)) {
    for (char** env = process_environ(); env && *env != nullptr; ++env) {
      std::string entry(*env);
      auto pos = entry.find('=');
      if (pos == std::string::npos) {
        continue;
      }
      env_map[entry.substr(0, pos)] = entry.substr(pos + 1);
    }
  }

  for (const auto& entry : CommandAccess::env_delta(cmd)) {
    const auto& key = entry.first;
    const auto& value = entry.second;
    if (value.has_value()) {
      env_map[key] = value.value();
    } else {
      env_map.erase(key);
    }
  }

  for (const auto& [key, value] : env_map) {
    std::string entry;
    entry.reserve(key.size() + 1 + value.size());
    entry.append(key);
    entry.push_back('=');
    entry.append(value);
    spec.envp.push_back(std::move(entry));
  }

  const bool output_mode = (mode == SpawnMode::output);

  std::optional<Stdio> stdin_value = CommandAccess::stdin_opt(cmd);
  std::optional<Stdio> stdout_value = CommandAccess::stdout_opt(cmd);
  std::optional<Stdio> stderr_value = CommandAccess::stderr_opt(cmd);
  if (override_stdio != nullptr) {
    if (override_stdio->stdin_override) {
      stdin_value = override_stdio->stdin_override;
    }
    if (override_stdio->stdout_override) {
      stdout_value = override_stdio->stdout_override;
    }
    if (override_stdio->stderr_override) {
      stderr_value = override_stdio->stderr_override;
    }
  }

  auto stdin_spec = resolve_stdio(stdin_value, false, StdioTarget::stdin);
  if (!stdin_spec) {
    return stdin_spec.error();
  }
  auto stdout_spec = resolve_stdio(stdout_value, output_mode, StdioTarget::stdout);
  if (!stdout_spec) {
    return stdout_spec.error();
  }
  auto stderr_spec = resolve_stdio(stderr_value, output_mode, StdioTarget::stderr);
  if (!stderr_spec) {
    return stderr_spec.error();
  }

  spec.stdin_spec = stdin_spec.value();
  spec.stdout_spec = stdout_spec.value();
  spec.stderr_spec = stderr_spec.value();

  if (spec.opts.merge_stderr_into_stdout) {
    spec.stderr_spec = StdioSpec{};
    spec.stderr_spec.kind = StdioSpec::Kind::dup_stdout;
  }

  return spec;
}

Result<PipelineSpec> lower_pipeline(const Pipeline& pipeline, SpawnMode mode) {
  const auto& stages = PipelineAccess::stages(pipeline);
  if (stages.empty()) {
    return Error{.code = make_error_code(errc::invalid_pipeline), .context = "pipeline"};
  }

  PipelineSpec spec;
  spec.pipefail = PipelineAccess::pipefail(pipeline);
  spec.new_process_group = PipelineAccess::new_process_group(pipeline);
  spec.stages.reserve(stages.size());

  const std::size_t stage_count = stages.size();
  for (std::size_t index = 0; index < stage_count; ++index) {
    PipelineStageSpec stage;
    stage.command = &stages[index];
    stage.mode = (index + 1 == stage_count) ? mode : SpawnMode::spawn;
    stage.stdin_from_prev = index > 0;
    stage.stdout_to_next = index + 1 < stage_count;

    if (index == 0 && PipelineAccess::stdin_opt(pipeline)) {
      stage.overrides.stdin_override = PipelineAccess::stdin_opt(pipeline);
    }
    if (index + 1 == stage_count && PipelineAccess::stdout_opt(pipeline)) {
      stage.overrides.stdout_override = PipelineAccess::stdout_opt(pipeline);
    }
    if (index + 1 == stage_count && PipelineAccess::stderr_opt(pipeline)) {
      stage.overrides.stderr_override = PipelineAccess::stderr_opt(pipeline);
    }

    spec.stages.push_back(std::move(stage));
  }

  return spec;
}

}  // namespace procly::internal
