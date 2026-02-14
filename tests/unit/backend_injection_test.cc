#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <optional>
#include <thread>
#include <vector>

#include "procly/child.hpp"
#include "procly/command.hpp"
#include "procly/internal/access.hpp"
#include "procly/internal/backend.hpp"
#include "procly/pipeline.hpp"

namespace procly {
namespace {

class FakeBackend final : public internal::Backend {
 public:
  struct WaitCall {
    int pid;
    std::optional<std::chrono::milliseconds> timeout;
    std::chrono::milliseconds kill_grace;
  };

  Result<internal::Spawned> spawn(const internal::SpawnSpec& spec) override {
    spawn_specs.push_back(spec);
    ++spawn_calls;
    if (fail_on_spawn_call > 0 && spawn_calls == fail_on_spawn_call) {
      return Error{make_error_code(errc::spawn_failed), "spawn"};
    }
    if (spawn_error) {
      return *spawn_error;
    }
    internal::Spawned spawned;
    spawned.pid = 100 + spawn_calls;
    spawned.new_process_group = spec.opts.new_process_group;
    if (spec.opts.new_process_group) {
      spawned.pgid = spawned.pid;
    }
    if (spec.process_group) {
      spawned.pgid = *spec.process_group;
    }
    return spawned;
  }

  Result<ExitStatus> wait(internal::Spawned& spawned,
                          std::optional<std::chrono::milliseconds> timeout,
                          std::chrono::milliseconds kill_grace) override {
    wait_calls.push_back({spawned.pid, timeout, kill_grace});
    if (wait_error) {
      return *wait_error;
    }
    return wait_result;
  }

  Result<std::optional<ExitStatus>> try_wait(internal::Spawned& spawned) override {
    try_wait_pids.push_back(spawned.pid);
    if (try_wait_error) {
      return *try_wait_error;
    }
    return try_wait_result;
  }

  Result<void> terminate(internal::Spawned& spawned) override {
    terminate_pids.push_back(spawned.pid);
    if (terminate_error) {
      return *terminate_error;
    }
    return {};
  }

  Result<void> kill(internal::Spawned& spawned) override {
    kill_pids.push_back(spawned.pid);
    if (kill_error) {
      return *kill_error;
    }
    return {};
  }

  Result<void> signal(internal::Spawned& spawned, int signo) override {
    signal_pids.push_back(spawned.pid);
    last_signal = signo;
    if (signal_error) {
      return *signal_error;
    }
    return {};
  }

  int spawn_calls = 0;
  int fail_on_spawn_call = -1;
  std::vector<internal::SpawnSpec> spawn_specs;
  std::vector<WaitCall> wait_calls;
  std::vector<int> try_wait_pids;
  std::vector<int> terminate_pids;
  std::vector<int> kill_pids;
  std::vector<int> signal_pids;
  int last_signal = 0;

  ExitStatus wait_result = ExitStatus::exited(0);
  std::optional<ExitStatus> try_wait_result;

  std::optional<Error> spawn_error;
  std::optional<Error> wait_error;
  std::optional<Error> try_wait_error;
  std::optional<Error> terminate_error;
  std::optional<Error> kill_error;
  std::optional<Error> signal_error;
};

}  // namespace

TEST(BackendInjectionTest, ScopedOverrideRestoresDefault) {
  FakeBackend backend;
  internal::Backend* before = &internal::default_backend();
  {
    internal::ScopedBackendOverride override_backend(backend);
    EXPECT_EQ(&internal::default_backend(), &backend);
  }
  EXPECT_EQ(&internal::default_backend(), before);
}

TEST(BackendInjectionTest, ScopedOverrideStacksAndRestores) {
  FakeBackend backend_a;
  FakeBackend backend_b;
  internal::Backend* before = &internal::default_backend();
  {
    internal::ScopedBackendOverride override_a(backend_a);
    EXPECT_EQ(&internal::default_backend(), &backend_a);
    {
      internal::ScopedBackendOverride override_b(backend_b);
      EXPECT_EQ(&internal::default_backend(), &backend_b);
    }
    EXPECT_EQ(&internal::default_backend(), &backend_a);
  }
  EXPECT_EQ(&internal::default_backend(), before);
}

TEST(BackendInjectionTest, ScopedOverrideVisibleAcrossThreads) {
  FakeBackend backend;
  internal::ScopedBackendOverride override_backend(backend);

  internal::Backend* observed = nullptr;
  std::thread worker([&] { observed = &internal::default_backend(); });
  worker.join();
  EXPECT_EQ(observed, &backend);
}

TEST(BackendInjectionTest, CommandStatusUsesInjectedBackend) {
  FakeBackend backend;
  backend.wait_result = ExitStatus::exited(7);
  internal::ScopedBackendOverride override_backend(backend);

  Command cmd("echo");
  auto status = cmd.status();
  ASSERT_TRUE(status.has_value());
  ASSERT_TRUE(status->code().has_value());
  EXPECT_EQ(status->code().value(), 7);
  EXPECT_EQ(backend.spawn_calls, 1);
  ASSERT_EQ(backend.wait_calls.size(), 1u);
}

TEST(BackendInjectionTest, CommandSpawnPropagatesBackendError) {
  FakeBackend backend;
  backend.spawn_error = Error{make_error_code(errc::spawn_failed), "spawn"};
  internal::ScopedBackendOverride override_backend(backend);

  Command cmd("echo");
  auto child = cmd.spawn();
  ASSERT_FALSE(child.has_value());
  EXPECT_EQ(child.error().code, make_error_code(errc::spawn_failed));
  EXPECT_EQ(backend.spawn_calls, 1);
}

TEST(BackendInjectionTest, ChildMethodsForwardToBackend) {
  FakeBackend backend;
  internal::ScopedBackendOverride override_backend(backend);

  internal::Spawned spawned;
  spawned.pid = 4242;
  Child child = internal::ChildAccess::from_spawned(spawned);

  auto wait_result = child.wait();
  ASSERT_TRUE(wait_result.has_value());
  EXPECT_EQ(backend.wait_calls.size(), 1u);
  EXPECT_EQ(backend.wait_calls[0].pid, 4242);

  auto try_wait_result = child.try_wait();
  ASSERT_TRUE(try_wait_result.has_value());
  EXPECT_EQ(backend.try_wait_pids.size(), 1u);
  EXPECT_EQ(backend.try_wait_pids[0], 4242);

  auto term_result = child.terminate();
  ASSERT_TRUE(term_result.has_value());
  EXPECT_EQ(backend.terminate_pids.size(), 1u);
  EXPECT_EQ(backend.terminate_pids[0], 4242);

  auto kill_result = child.kill();
  ASSERT_TRUE(kill_result.has_value());
  EXPECT_EQ(backend.kill_pids.size(), 1u);
  EXPECT_EQ(backend.kill_pids[0], 4242);

  auto signal_result = child.signal(SIGUSR1);
  ASSERT_TRUE(signal_result.has_value());
  EXPECT_EQ(backend.signal_pids.size(), 1u);
  EXPECT_EQ(backend.signal_pids[0], 4242);
  EXPECT_EQ(backend.last_signal, SIGUSR1);
}

TEST(BackendInjectionTest, ChildPropagatesBackendErrors) {
  FakeBackend backend;
  backend.terminate_error = Error{make_error_code(errc::kill_failed), "terminate"};
  backend.kill_error = Error{make_error_code(errc::kill_failed), "kill"};
  backend.signal_error = Error{make_error_code(errc::kill_failed), "signal"};
  internal::ScopedBackendOverride override_backend(backend);

  internal::Spawned spawned;
  spawned.pid = 9001;
  Child child = internal::ChildAccess::from_spawned(spawned);

  auto terminate_result = child.terminate();
  ASSERT_FALSE(terminate_result.has_value());
  EXPECT_EQ(terminate_result.error().code, make_error_code(errc::kill_failed));

  auto kill_result = child.kill();
  ASSERT_FALSE(kill_result.has_value());
  EXPECT_EQ(kill_result.error().code, make_error_code(errc::kill_failed));

  auto signal_result = child.signal(SIGTERM);
  ASSERT_FALSE(signal_result.has_value());
  EXPECT_EQ(signal_result.error().code, make_error_code(errc::kill_failed));
}

TEST(BackendInjectionTest, TryWaitReturnsStatusOrEmpty) {
  FakeBackend backend;
  internal::ScopedBackendOverride override_backend(backend);

  internal::Spawned spawned;
  spawned.pid = 5050;
  Child child = internal::ChildAccess::from_spawned(spawned);

  backend.try_wait_result = std::nullopt;
  auto empty_result = child.try_wait();
  ASSERT_TRUE(empty_result.has_value());
  EXPECT_FALSE(empty_result->has_value());

  backend.try_wait_result = ExitStatus::exited(9);
  auto status_result = child.try_wait();
  ASSERT_TRUE(status_result.has_value());
  ASSERT_TRUE(status_result->has_value());
  EXPECT_EQ(status_result->value().code().value_or(-1), 9);
}

TEST(BackendInjectionTest, PipelineNewProcessGroupPropagates) {
  FakeBackend backend;
  internal::ScopedBackendOverride override_backend(backend);

  Pipeline pipeline = Command("echo") | Command("cat");
  pipeline.new_process_group(true);

  auto child_result = pipeline.spawn();
  ASSERT_TRUE(child_result.has_value());
  ASSERT_EQ(backend.spawn_specs.size(), 2u);

  const auto& first_spec = backend.spawn_specs[0];
  const auto& second_spec = backend.spawn_specs[1];

  EXPECT_TRUE(first_spec.opts.new_process_group);
  EXPECT_FALSE(second_spec.opts.new_process_group);
  ASSERT_TRUE(second_spec.process_group.has_value());
  EXPECT_EQ(second_spec.process_group.value(), 101);
}

TEST(BackendInjectionTest, PipelineSpawnStopsOnError) {
  FakeBackend backend;
  backend.fail_on_spawn_call = 2;
  internal::ScopedBackendOverride override_backend(backend);

  Pipeline pipeline = Command("echo") | Command("cat") | Command("cat");
  auto child_result = pipeline.spawn();
  ASSERT_FALSE(child_result.has_value());
  EXPECT_EQ(child_result.error().code, make_error_code(errc::spawn_failed));
  EXPECT_EQ(backend.spawn_calls, 2);
  EXPECT_EQ(backend.spawn_specs.size(), 2u);
}

TEST(BackendInjectionTest, PipelineSpawnFailureCleansUpStartedStages) {
  FakeBackend backend;
  backend.fail_on_spawn_call = 2;
  internal::ScopedBackendOverride override_backend(backend);

  Pipeline pipeline = Command("echo") | Command("cat") | Command("cat");
  auto child_result = pipeline.spawn();
  ASSERT_FALSE(child_result.has_value());
  EXPECT_EQ(child_result.error().code, make_error_code(errc::spawn_failed));

  ASSERT_EQ(backend.kill_pids.size(), 1u);
  EXPECT_EQ(backend.kill_pids[0], 101);
  ASSERT_EQ(backend.wait_calls.size(), 1u);
  EXPECT_EQ(backend.wait_calls[0].pid, 101);
}

TEST(BackendInjectionTest, PipelineGroupTerminateAndKillUseLeader) {
  FakeBackend backend;
  internal::ScopedBackendOverride override_backend(backend);

  Pipeline pipeline = Command("echo") | Command("cat");
  pipeline.new_process_group(true);
  auto child_result = pipeline.spawn();
  ASSERT_TRUE(child_result.has_value());

  auto terminate_result = child_result->terminate();
  ASSERT_TRUE(terminate_result.has_value());
  EXPECT_EQ(backend.terminate_pids.size(), 1u);
  EXPECT_EQ(backend.terminate_pids[0], 101);

  auto kill_result = child_result->kill();
  ASSERT_TRUE(kill_result.has_value());
  EXPECT_EQ(backend.kill_pids.size(), 1u);
  EXPECT_EQ(backend.kill_pids[0], 101);
}

TEST(BackendInjectionTest, PipelineTerminateAndKillPerStageWithoutGroup) {
  FakeBackend backend;
  internal::ScopedBackendOverride override_backend(backend);

  Pipeline pipeline = Command("echo") | Command("cat") | Command("cat");
  auto child_result = pipeline.spawn();
  ASSERT_TRUE(child_result.has_value());

  auto terminate_result = child_result->terminate();
  ASSERT_TRUE(terminate_result.has_value());
  EXPECT_EQ(backend.terminate_pids.size(), 3u);

  auto kill_result = child_result->kill();
  ASSERT_TRUE(kill_result.has_value());
  EXPECT_EQ(backend.kill_pids.size(), 3u);
}

TEST(BackendInjectionTest, CommandOutputUsesInjectedBackend) {
  FakeBackend backend;
  backend.wait_result = ExitStatus::exited(3);
  internal::ScopedBackendOverride override_backend(backend);

  Command cmd("echo");
  auto output = cmd.output();
  ASSERT_TRUE(output.has_value());
  ASSERT_TRUE(output->status.code().has_value());
  EXPECT_EQ(output->status.code().value(), 3);
  EXPECT_EQ(backend.spawn_calls, 1);
  EXPECT_EQ(backend.wait_calls.size(), 1u);
}

}  // namespace procly
