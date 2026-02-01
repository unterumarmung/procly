#pragma once

#include <cstdint>
#include <optional>

#include "procly/command.hpp"
#include "procly/internal/backend.hpp"
#include "procly/pipeline.hpp"
#include "procly/result.hpp"

namespace procly::internal {

enum class SpawnMode : std::uint8_t { spawn, output };

struct StdioOverride {
  std::optional<Stdio> stdin_override;
  std::optional<Stdio> stdout_override;
  std::optional<Stdio> stderr_override;
};

struct PipelineStageSpec {
  const Command* command = nullptr;
  SpawnMode mode = SpawnMode::spawn;
  StdioOverride overrides;
  bool stdin_from_prev = false;
  bool stdout_to_next = false;
};

struct PipelineSpec {
  std::vector<PipelineStageSpec> stages;
  bool pipefail = false;
  bool new_process_group = false;
};

struct CommandAccess {
  static const std::vector<std::string>& argv(const Command& cmd) { return cmd.argv_; }
  static const std::optional<std::filesystem::path>& cwd(const Command& cmd) { return cmd.cwd_; }
  static bool inherit_env(const Command& cmd) { return cmd.inherit_env_; }
  static const std::map<std::string, std::optional<std::string>, std::less<>>& env_delta(
      const Command& cmd) {
    return cmd.env_delta_;
  }
  static const std::optional<Stdio>& stdin_opt(const Command& cmd) { return cmd.stdin_; }
  static const std::optional<Stdio>& stdout_opt(const Command& cmd) { return cmd.stdout_; }
  static const std::optional<Stdio>& stderr_opt(const Command& cmd) { return cmd.stderr_; }
  static const SpawnOptions& options(const Command& cmd) { return cmd.opts_; }
};

struct ChildAccess;
struct PipelineAccess;

Result<SpawnSpec> lower_command(const Command& cmd, SpawnMode mode,
                                const StdioOverride* override_stdio);
Result<PipelineSpec> lower_pipeline(const Pipeline& pipeline, SpawnMode mode);

}  // namespace procly::internal
