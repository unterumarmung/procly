#include "procly/child.hpp"

#include "procly/internal/access.hpp"
#include "procly/internal/backend.hpp"
#include "procly/internal/concurrent_use_guard.hpp"
#include "procly/internal/lowering.hpp"

namespace procly {

struct Child::Impl {
  explicit Impl(internal::Spawned spawned) : spawned_(spawned) {
    if (spawned_.stdin_fd) {
      stdin_pipe.emplace(*spawned_.stdin_fd);
    }
    if (spawned_.stdout_fd) {
      stdout_pipe.emplace(*spawned_.stdout_fd);
    }
    if (spawned_.stderr_fd) {
      stderr_pipe.emplace(*spawned_.stderr_fd);
    }
  }

  internal::Spawned spawned_;
  std::optional<PipeWriter> stdin_pipe;
  std::optional<PipeReader> stdout_pipe;
  std::optional<PipeReader> stderr_pipe;
  internal::ConcurrentUseGuard concurrent_use;
};

Child::Child() = default;

Child::Child(Child&& other) noexcept {
  if (other.impl_) {
    auto use = other.impl_->concurrent_use.enter("Child");
    (void)use;
    impl_ = std::move(other.impl_);
  }
}

Child& Child::operator=(Child&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  if (other.impl_) {
    auto other_use = other.impl_->concurrent_use.enter("Child");
    (void)other_use;
  }
  impl_ = std::move(other.impl_);
  return *this;
}

Child::~Child() = default;

namespace internal {

Child ChildAccess::from_spawned(Spawned spawned) {
  Child child;
  ChildAccess::impl(child) = std::make_unique<Child::Impl>(spawned);
  return child;
}

}  // namespace internal

int Child::id() const noexcept {
  if (!impl_) {
    return -1;
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  return impl_->spawned_.pid;
}

std::optional<PipeWriter> Child::take_stdin() noexcept {
  if (!impl_) {
    return std::nullopt;
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  auto pipe = std::move(impl_->stdin_pipe);
  impl_->stdin_pipe.reset();
  return pipe;
}

std::optional<PipeReader> Child::take_stdout() noexcept {
  if (!impl_) {
    return std::nullopt;
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  auto pipe = std::move(impl_->stdout_pipe);
  impl_->stdout_pipe.reset();
  return pipe;
}

std::optional<PipeReader> Child::take_stderr() noexcept {
  if (!impl_) {
    return std::nullopt;
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  auto pipe = std::move(impl_->stderr_pipe);
  impl_->stderr_pipe.reset();
  return pipe;
}

Result<ExitStatus> Child::wait() {
  if (!impl_) {
    return Error{.code = make_error_code(errc::wait_failed), .context = "wait"};
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  auto wait_result = internal::backend_for(impl_->spawned_)
                         .wait(impl_->spawned_, std::nullopt, std::chrono::milliseconds(0));
  if (!wait_result) {
    return wait_result.error();
  }
  return wait_result->status;
}

Result<std::optional<ExitStatus>> Child::try_wait() {
  if (!impl_) {
    return Error{.code = make_error_code(errc::wait_failed), .context = "try_wait"};
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  return internal::backend_for(impl_->spawned_).try_wait(impl_->spawned_);
}

Result<WaitResult> Child::wait(WaitOptions options) {
  if (!impl_) {
    return Error{.code = make_error_code(errc::wait_failed), .context = "wait"};
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  return internal::backend_for(impl_->spawned_)
      .wait(impl_->spawned_, options.timeout, options.kill_grace);
}

Result<void> Child::terminate() {
  if (!impl_) {
    return Error{.code = make_error_code(errc::kill_failed), .context = "terminate"};
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  return internal::backend_for(impl_->spawned_).terminate(impl_->spawned_);
}

Result<void> Child::kill() {
  if (!impl_) {
    return Error{.code = make_error_code(errc::kill_failed), .context = "kill"};
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  return internal::backend_for(impl_->spawned_).kill(impl_->spawned_);
}

Result<void> Child::signal(int signo) {
  if (!impl_) {
    return Error{.code = make_error_code(errc::kill_failed), .context = "signal"};
  }
  auto use = impl_->concurrent_use.enter("Child");
  (void)use;
  return internal::backend_for(impl_->spawned_).signal(impl_->spawned_, signo);
}

}  // namespace procly
