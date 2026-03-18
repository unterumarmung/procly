#include "procly/pipeline.hpp"

#include <utility>

#include "procly/child.hpp"
#include "procly/internal/access.hpp"
#include "procly/internal/backend.hpp"
#include "procly/internal/fd.hpp"
#include "procly/internal/io_drain.hpp"
#include "procly/internal/lowering.hpp"

namespace procly {

Pipeline& Pipeline::pipefail(bool enabled) {
  auto use = concurrent_use_.enter("Pipeline");
  (void)use;
  pipefail_ = enabled;
  return *this;
}

Pipeline& Pipeline::new_process_group(bool enabled) {
  auto use = concurrent_use_.enter("Pipeline");
  (void)use;
  new_pgrp_ = enabled;
  return *this;
}

Pipeline& Pipeline::stdin(Stdio value) {
  auto use = concurrent_use_.enter("Pipeline");
  (void)use;
  stdin_ = std::move(value);
  return *this;
}

Pipeline& Pipeline::stdout(Stdio value) {
  auto use = concurrent_use_.enter("Pipeline");
  (void)use;
  stdout_ = std::move(value);
  return *this;
}

Pipeline& Pipeline::stderr(Stdio value) {
  auto use = concurrent_use_.enter("Pipeline");
  (void)use;
  stderr_ = std::move(value);
  return *this;
}

Pipeline operator|(Command left, Command right) {
  Pipeline pipeline;
  pipeline.stages_.push_back(std::move(left));
  pipeline.stages_.push_back(std::move(right));
  return pipeline;
}

Pipeline operator|(Pipeline left, Command right) {
  left.stages_.push_back(std::move(right));
  return left;
}

struct PipelineChild::Impl {
  std::vector<internal::Spawned> spawned;
  bool pipefail = false;
  bool new_process_group = false;
  std::optional<int> pgid;
  std::optional<PipeWriter> stdin_pipe;
  std::optional<PipeReader> stdout_pipe;
  std::optional<PipeReader> stderr_pipe;
  internal::ConcurrentUseGuard concurrent_use;

  void close_owned_pipes() noexcept {
    if (stdin_pipe) {
      stdin_pipe->close();
      stdin_pipe.reset();
    }
    if (!spawned.empty()) {
      spawned.front().stdin_fd.reset();
    }

    if (stdout_pipe) {
      stdout_pipe->close();
      stdout_pipe.reset();
    }
    if (!spawned.empty()) {
      spawned.back().stdout_fd.reset();
    }

    if (stderr_pipe) {
      stderr_pipe->close();
      stderr_pipe.reset();
    }
    if (!spawned.empty()) {
      spawned.back().stderr_fd.reset();
    }
  }

  void cleanup_on_drop() noexcept {
    close_owned_pipes();
    for (auto& stage : spawned) {
      if (stage.terminal_result || stage.pid <= 0) {
        continue;
      }
      (void)internal::backend_for(stage).wait(stage, std::nullopt, std::chrono::milliseconds(0));
    }
  }
};

void cleanup_partially_spawned_pipeline(std::vector<internal::Spawned>* spawned,
                                        bool new_process_group) {
  if (spawned == nullptr || spawned->empty()) {
    return;
  }

  // Kill first so failed pipeline creation cannot leave background processes running.
  if (new_process_group) {
    (void)internal::backend_for(spawned->front()).kill(spawned->front());
  } else {
    for (auto& stage : *spawned) {
      (void)internal::backend_for(stage).kill(stage);
    }
  }

  // Reap every stage to avoid zombies after a mid-pipeline spawn failure.
  for (auto& stage : *spawned) {
    (void)internal::backend_for(stage).wait(stage, std::nullopt, std::chrono::milliseconds(0));
  }
}

static Result<PipelineChild> spawn_pipeline(const Pipeline& pipeline, internal::SpawnMode mode) {
  auto pipeline_spec_result = internal::lower_pipeline(pipeline, mode);
  if (!pipeline_spec_result) {
    return pipeline_spec_result.error();
  }
  const auto& pipeline_spec = pipeline_spec_result.value();

  const std::size_t stage_count = pipeline_spec.stages.size();
  std::vector<std::pair<internal::unique_fd, internal::unique_fd>> pipes;
  if (stage_count > 1) {
    pipes.reserve(stage_count - 1);
    for (std::size_t i = 0; i + 1 < stage_count; ++i) {
      auto pipe_result = internal::create_pipe();
      if (!pipe_result) {
        return pipe_result.error();
      }
      pipes.push_back(std::move(pipe_result.value()));
    }
  }

  struct PreparedStage {
    internal::SpawnSpec spec;
    bool joins_pipeline_group = false;
  };

  std::vector<PreparedStage> prepared_stages;
  prepared_stages.reserve(stage_count);

  for (std::size_t index = 0; index < stage_count; ++index) {
    const auto& stage_spec = pipeline_spec.stages[index];
    internal::StdioOverride override_stdio = stage_spec.overrides;

    if (stage_spec.stdin_from_prev) {
      override_stdio.stdin_override = Stdio::fd(pipes[index - 1].first.get());
    }
    if (stage_spec.stdout_to_next) {
      override_stdio.stdout_override = Stdio::fd(pipes[index].second.get());
    }

    auto spec_result =
        internal::lower_command(*stage_spec.command, stage_spec.mode, &override_stdio);
    if (!spec_result) {
      return spec_result.error();
    }

    PreparedStage prepared{
        .spec = std::move(spec_result.value()),
        .joins_pipeline_group = pipeline_spec.new_process_group && index > 0,
    };
    if (pipeline_spec.new_process_group && index == 0) {
      prepared.spec.opts.new_process_group = true;
    }
    prepared_stages.push_back(std::move(prepared));
  }

  std::vector<internal::Spawned> spawned;
  spawned.reserve(stage_count);

  std::optional<int> pipeline_pgid;
  auto& backend = internal::default_backend();

  for (std::size_t index = 0; index < stage_count; ++index) {
    auto spec = prepared_stages[index].spec;
    if (prepared_stages[index].joins_pipeline_group) {
      spec.process_group = pipeline_pgid;
    } else {
      spec.process_group.reset();
    }

    auto spawned_result = backend.spawn(spec);
    if (!spawned_result) {
      cleanup_partially_spawned_pipeline(&spawned, pipeline_spec.new_process_group);
      return spawned_result.error();
    }
    spawned_result->backend = &backend;

    if (internal::PipelineAccess::new_process_group(pipeline) && !pipeline_pgid) {
      pipeline_pgid = spawned_result->pgid;
    }

    spawned.push_back(spawned_result.value());
  }

  PipelineChild child;
  auto impl = std::make_unique<PipelineChild::Impl>();
  impl->spawned = std::move(spawned);
  impl->pipefail = pipeline_spec.pipefail;
  impl->new_process_group = pipeline_spec.new_process_group;
  impl->pgid = pipeline_pgid;

  if (!impl->spawned.empty()) {
    auto& first = impl->spawned.front();
    auto& last = impl->spawned.back();
    if (first.stdin_fd) {
      impl->stdin_pipe.emplace(*first.stdin_fd);
    }
    if (last.stdout_fd) {
      impl->stdout_pipe.emplace(*last.stdout_fd);
    }
    if (last.stderr_fd) {
      impl->stderr_pipe.emplace(*last.stderr_fd);
    }
  }

  internal::PipelineAccess::impl(child) = std::move(impl);
  return child;
}

Result<PipelineChild> Pipeline::spawn() const {
  auto use = concurrent_use_.enter("Pipeline");
  (void)use;
  return spawn_pipeline(*this, internal::SpawnMode::spawn);
}

Result<ExitStatus> Pipeline::status() const {
  auto use = concurrent_use_.enter("Pipeline");
  (void)use;
  auto child_result = spawn_pipeline(*this, internal::SpawnMode::spawn);
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

  auto status_result = child_result->wait();
  if (!status_result) {
    return status_result.error();
  }
  return status_result->aggregate;
}

Result<Output> Pipeline::output() const {
  auto use = concurrent_use_.enter("Pipeline");
  (void)use;
  auto child_result = spawn_pipeline(*this, internal::SpawnMode::output);
  if (!child_result) {
    return child_result.error();
  }

  auto stdin_pipe = child_result->take_stdin();
  if (stdin_pipe) {
    stdin_pipe->close();
  }

  auto stdout_pipe = child_result->take_stdout();
  auto stderr_pipe = child_result->take_stderr();

  auto drained = internal::drain_pipes(stdout_pipe ? &*stdout_pipe : nullptr,
                                       stderr_pipe ? &*stderr_pipe : nullptr);
  if (!drained) {
    return drained.error();
  }

  auto status_result = child_result->wait();
  if (!status_result) {
    return status_result.error();
  }

  Output output;
  output.status = status_result->aggregate;
  output.stdout_data = std::move(drained->stdout_data);
  output.stderr_data = std::move(drained->stderr_data);
  return output;
}

PipelineChild::PipelineChild() = default;

PipelineChild::PipelineChild(PipelineChild&& other) noexcept {
  if (other.impl_) {
    auto use = other.impl_->concurrent_use.enter("PipelineChild");
    (void)use;
    impl_ = std::move(other.impl_);
  }
}

PipelineChild& PipelineChild::operator=(PipelineChild&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  if (other.impl_) {
    auto other_use = other.impl_->concurrent_use.enter("PipelineChild");
    (void)other_use;
  }
  impl_ = std::move(other.impl_);
  return *this;
}

PipelineChild::~PipelineChild() {
  if (!impl_) {
    return;
  }
  auto use = impl_->concurrent_use.enter("PipelineChild");
  (void)use;
  impl_->cleanup_on_drop();
}

std::optional<PipeWriter> PipelineChild::take_stdin() noexcept {
  if (!impl_) {
    return std::nullopt;
  }
  auto use = impl_->concurrent_use.enter("PipelineChild");
  (void)use;
  auto pipe = std::move(impl_->stdin_pipe);
  impl_->stdin_pipe.reset();
  if (!impl_->spawned.empty()) {
    impl_->spawned.front().stdin_fd.reset();
  }
  return pipe;
}

std::optional<PipeReader> PipelineChild::take_stdout() noexcept {
  if (!impl_) {
    return std::nullopt;
  }
  auto use = impl_->concurrent_use.enter("PipelineChild");
  (void)use;
  auto pipe = std::move(impl_->stdout_pipe);
  impl_->stdout_pipe.reset();
  if (!impl_->spawned.empty()) {
    impl_->spawned.back().stdout_fd.reset();
  }
  return pipe;
}

std::optional<PipeReader> PipelineChild::take_stderr() noexcept {
  if (!impl_) {
    return std::nullopt;
  }
  auto use = impl_->concurrent_use.enter("PipelineChild");
  (void)use;
  auto pipe = std::move(impl_->stderr_pipe);
  impl_->stderr_pipe.reset();
  if (!impl_->spawned.empty()) {
    impl_->spawned.back().stderr_fd.reset();
  }
  return pipe;
}

Result<PipelineStatus> PipelineChild::wait() {
  if (!impl_) {
    return Error{.code = make_error_code(errc::wait_failed), .context = "wait"};
  }
  auto use = impl_->concurrent_use.enter("PipelineChild");
  (void)use;

  PipelineStatus status;
  status.stages.reserve(impl_->spawned.size());

  for (auto& spawned : impl_->spawned) {
    auto wait_result =
        internal::backend_for(spawned).wait(spawned, std::nullopt, std::chrono::milliseconds(0));
    if (!wait_result) {
      return wait_result.error();
    }
    status.stages.push_back(wait_result->status);
  }

  if (status.stages.empty()) {
    return Error{.code = make_error_code(errc::invalid_pipeline), .context = "wait"};
  }

  if (!impl_->pipefail) {
    status.aggregate = status.stages.back();
    return status;
  }

  for (auto it = status.stages.rbegin(); it != status.stages.rend(); ++it) {
    if (!it->success()) {
      status.aggregate = *it;
      return status;
    }
  }
  status.aggregate = status.stages.back();
  return status;
}

Result<void> PipelineChild::terminate() {
  if (!impl_) {
    return Error{.code = make_error_code(errc::kill_failed), .context = "terminate"};
  }
  auto use = impl_->concurrent_use.enter("PipelineChild");
  (void)use;

  if (impl_->new_process_group && !impl_->spawned.empty()) {
    return internal::backend_for(impl_->spawned.front()).terminate(impl_->spawned.front());
  }

  for (auto& spawned : impl_->spawned) {
    auto result = internal::backend_for(spawned).terminate(spawned);
    if (!result) {
      return result;
    }
  }
  return {};
}

Result<void> PipelineChild::kill() {
  if (!impl_) {
    return Error{.code = make_error_code(errc::kill_failed), .context = "kill"};
  }
  auto use = impl_->concurrent_use.enter("PipelineChild");
  (void)use;

  if (impl_->new_process_group && !impl_->spawned.empty()) {
    return internal::backend_for(impl_->spawned.front()).kill(impl_->spawned.front());
  }

  for (auto& spawned : impl_->spawned) {
    auto result = internal::backend_for(spawned).kill(spawned);
    if (!result) {
      return result;
    }
  }
  return {};
}

}  // namespace procly
