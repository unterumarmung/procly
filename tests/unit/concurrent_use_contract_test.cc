#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "procly/command.hpp"
#include "procly/internal/backend.hpp"
#include "procly/internal/fd.hpp"
#include "procly/pipeline.hpp"

namespace procly {
namespace {

class BlockingBackend final : public internal::Backend {
 public:
  Result<internal::Spawned> spawn(const internal::SpawnSpec& spec) override {
    internal::Spawned spawned;
    spawned.pid = next_pid_++;
    spawned.new_process_group = spec.opts.new_process_group;
    if (spec.opts.new_process_group) {
      spawned.pgid = spawned.pid;
    }
    if (spec.process_group) {
      spawned.pgid = *spec.process_group;
    }
    return spawned;
  }

  Result<WaitResult> wait(internal::Spawned& spawned,
                          std::optional<std::chrono::milliseconds> timeout,
                          std::chrono::milliseconds kill_grace) override {
    (void)timeout;
    (void)kill_grace;
    if (spawned.terminal_result) {
      return *spawned.terminal_result;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    blocked_ = true;
    blocked_cv_.notify_all();
    release_cv_.wait(lock, [&] { return released_; });

    internal::cache_terminal_result(spawned, WaitResult{.status = ExitStatus::exited(0)});
    return *spawned.terminal_result;
  }

  Result<std::optional<ExitStatus>> try_wait(internal::Spawned& spawned) override {
    if (spawned.terminal_result) {
      return std::optional<ExitStatus>(spawned.terminal_result->status);
    }
    return std::optional<ExitStatus>{};
  }

  Result<void> terminate(internal::Spawned& spawned) override {
    (void)spawned;
    return {};
  }

  Result<void> kill(internal::Spawned& spawned) override {
    (void)spawned;
    return {};
  }

  Result<void> signal(internal::Spawned& spawned, int signo) override {
    (void)spawned;
    (void)signo;
    return {};
  }

  void wait_until_blocked() {
    std::unique_lock<std::mutex> lock(mutex_);
    blocked_cv_.wait(lock, [&] { return blocked_; });
  }

 private:
  int next_pid_ = 100;
  std::mutex mutex_;
  std::condition_variable blocked_cv_;
  std::condition_variable release_cv_;
  bool blocked_ = false;
  bool released_ = false;
};

}  // namespace

#ifndef NDEBUG
TEST(ConcurrentUseContractTest, SharedCommandConcurrentUseDies) {
  EXPECT_DEATH(([] {
                 Command cmd("/bin/sleep");
                 cmd.arg("1");
                 std::thread worker([&] { (void)cmd.status(); });
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 cmd.arg("later");
                 worker.join();
                 std::_Exit(0);
               })(),
               "concurrent use of non-thread-safe object: Command");
}

TEST(ConcurrentUseContractTest, SharedChildConcurrentUseDies) {
  EXPECT_DEATH(([] {
                 BlockingBackend backend;
                 internal::ScopedBackendOverride override_backend(backend);

                 auto child_result = Command("echo").spawn();
                 ASSERT_TRUE(child_result.has_value());
                 Child child = std::move(child_result.value());

                 std::thread worker([&] { (void)child.wait(); });
                 backend.wait_until_blocked();
                 (void)child.terminate();
                 worker.join();
                 std::_Exit(0);
               })(),
               "concurrent use of non-thread-safe object: Child");
}

TEST(ConcurrentUseContractTest, SharedPipelineConcurrentUseDies) {
  EXPECT_DEATH(([] {
                 Pipeline pipeline = Command("/bin/sleep").arg("1") | Command("/bin/cat");
                 std::thread worker([&] { (void)pipeline.status(); });
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 pipeline.pipefail(true);
                 worker.join();
                 std::_Exit(0);
               })(),
               "concurrent use of non-thread-safe object: Pipeline");
}

TEST(ConcurrentUseContractTest, SharedPipelineChildConcurrentUseDies) {
  EXPECT_DEATH(([] {
                 BlockingBackend backend;
                 internal::ScopedBackendOverride override_backend(backend);

                 auto child_result = (Command("echo") | Command("cat")).spawn();
                 ASSERT_TRUE(child_result.has_value());
                 PipelineChild child = std::move(child_result.value());

                 std::thread worker([&] { (void)child.wait(); });
                 backend.wait_until_blocked();
                 (void)child.kill();
                 worker.join();
                 std::_Exit(0);
               })(),
               "concurrent use of non-thread-safe object: PipelineChild");
}

TEST(ConcurrentUseContractTest, SharedPipeReaderConcurrentUseDies) {
  EXPECT_DEATH(([] {
                 auto pipe_result = internal::create_pipe();
                 ASSERT_TRUE(pipe_result.has_value());

                 PipeReader reader(pipe_result->first.release());
                 PipeWriter writer(pipe_result->second.release());

                 std::thread worker([&] { (void)reader.read_all(); });
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 reader.close();
                 worker.join();
                 std::_Exit(0);
               })(),
               "concurrent use of non-thread-safe object: PipeReader");
}

TEST(ConcurrentUseContractTest, SharedPipeWriterConcurrentUseDies) {
  EXPECT_DEATH(([] {
                 auto pipe_result = internal::create_pipe();
                 ASSERT_TRUE(pipe_result.has_value());

                 int read_fd = pipe_result->first.release();
                 int write_fd = pipe_result->second.release();

                 int original_flags = ::fcntl(write_fd, F_GETFL);
                 ASSERT_NE(original_flags, -1);
                 ASSERT_NE(::fcntl(write_fd, F_SETFL, original_flags | O_NONBLOCK), -1);

                 std::array<char, 4096> fill{};
                 while (true) {
                   ssize_t rv = ::write(write_fd, fill.data(), fill.size());
                   if (rv > 0) {
                     continue;
                   }
                   ASSERT_EQ(rv, -1);
                   ASSERT_EQ(errno, EAGAIN);
                   break;
                 }

                 ASSERT_NE(::fcntl(write_fd, F_SETFL, original_flags), -1);

                 PipeReader reader(read_fd);
                 PipeWriter writer(write_fd);

                 std::thread worker([&] { (void)writer.write_all("x"); });
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 writer.close();
                 worker.join();
                 std::_Exit(0);
               })(),
               "concurrent use of non-thread-safe object: PipeWriter");
}
#endif

}  // namespace procly
