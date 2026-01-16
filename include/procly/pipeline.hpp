#pragma once

#include <optional>
#include <vector>

#include "procly/command.hpp"
#include "procly/pipe.hpp"
#include "procly/result.hpp"
#include "procly/status.hpp"

namespace procly {

namespace internal {
/// @brief Internal access helper for Pipeline and PipelineChild.
struct PipelineAccess;
}  // namespace internal

/// @brief Aggregate pipeline status.
struct PipelineStatus {
  /// @brief Exit status for each stage, in order.
  std::vector<ExitStatus> stages;
  /// @brief Aggregate status using pipefail policy.
  ExitStatus aggregate;
};

/// @brief Running pipeline handle (forward declaration).
class PipelineChild;

/// @brief Pipeline of multiple commands connected by pipes.
class Pipeline {
 public:
  /// @brief Construct an empty pipeline.
  Pipeline() = default;

  /// @brief Enable pipefail behavior.
  Pipeline& pipefail(bool enabled = true);
  /// @brief Spawn pipeline in a new process group.
  Pipeline& new_process_group(bool enabled = true);

  /// @brief Configure stdin for the first stage.
  Pipeline& stdin(Stdio value);
  /// @brief Configure stdout for the last stage.
  Pipeline& stdout(Stdio value);
  /// @brief Configure stderr for the last stage.
  Pipeline& stderr(Stdio value);

  /// @brief Number of stages.
  [[nodiscard]] std::size_t size() const noexcept { return stages_.size(); }

  /// @brief Spawn the pipeline without waiting.
  [[nodiscard]] Result<PipelineChild> spawn() const;
  /// @brief Spawn and wait, returning aggregate exit status.
  [[nodiscard]] Result<ExitStatus> status() const;
  /// @brief Spawn, capture output from last stage, and wait.
  [[nodiscard]] Result<Output> output() const;

 private:
  /// @brief Commands making up the pipeline in order.
  std::vector<Command> stages_;
  /// @brief Whether to apply pipefail semantics to aggregate status.
  bool pipefail_ = false;
  /// @brief Whether to spawn the pipeline in a new process group.
  bool new_pgrp_ = false;
  /// @brief Optional stdin configuration for the first stage.
  std::optional<Stdio> stdin_;
  /// @brief Optional stdout configuration for the last stage.
  std::optional<Stdio> stdout_;
  /// @brief Optional stderr configuration for the last stage.
  std::optional<Stdio> stderr_;

  friend Pipeline operator|(Command left, Command right);
  friend Pipeline operator|(Pipeline left, Command right);
  friend struct internal::PipelineAccess;
};

/// @brief Create a pipeline from two commands.
Pipeline operator|(Command left, Command right);
/// @brief Append a command to an existing pipeline.
Pipeline operator|(Pipeline left, Command right);

/// @brief Running pipeline handle.
class PipelineChild {
 public:
  /// @brief Opaque implementation type.
  struct Impl;

  /// @brief Construct an empty pipeline handle.
  PipelineChild() = default;
  /// @brief Move-construct a pipeline handle.
  PipelineChild(PipelineChild&& other) noexcept;
  /// @brief Move-assign a pipeline handle.
  PipelineChild& operator=(PipelineChild&& other) noexcept;
  /// @brief Destroy the pipeline handle.
  ~PipelineChild();

  /// @brief Take ownership of stdin pipe, if present.
  std::optional<PipeWriter> take_stdin() noexcept;
  /// @brief Take ownership of stdout pipe, if present.
  std::optional<PipeReader> take_stdout() noexcept;
  /// @brief Take ownership of stderr pipe, if present.
  std::optional<PipeReader> take_stderr() noexcept;

  /// @brief Wait for pipeline completion.
  Result<PipelineStatus> wait();
  /// @brief Send terminate to all stages (or process group).
  Result<void> terminate();
  /// @brief Send kill to all stages (or process group).
  Result<void> kill();

 private:
  /// @brief Owned implementation state.
  std::unique_ptr<Impl> impl_;

  friend struct internal::PipelineAccess;
};

}  // namespace procly
