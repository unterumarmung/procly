#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "procly/command.hpp"
#include "procly/pipeline.hpp"
#include "tests/helpers/runfiles_support.hpp"

namespace procly {
namespace {

std::string helper_path() {
  const auto& argv = ::testing::internal::GetArgvs();
  if (argv.empty()) {
    ADD_FAILURE() << "argv0 missing";
    return "";
  }
  auto path = procly::support::helper_path(argv[0].c_str());
  if (path.empty()) {
    ADD_FAILURE() << "helper path not found";
  }
  return path;
}

}  // namespace

TEST(ConcurrencySmokeTest, ParallelCommandOutput) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  constexpr int kThreads = 4;
  constexpr int kRunsPerThread = 5;
  std::vector<std::thread> threads;
  std::vector<std::optional<Error>> errors(kThreads);

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i] {
      for (int run = 0; run < kRunsPerThread; ++run) {
        Command cmd(helper);
        cmd.arg("--stdout-bytes").arg(std::to_string(128 + i * 16 + run));
        cmd.arg("--stderr-bytes").arg(std::to_string(32 + run));
        auto output = cmd.output();
        if (!output) {
          errors[i] = output.error();
          return;
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  for (int i = 0; i < kThreads; ++i) {
    ASSERT_FALSE(errors[i].has_value()) << errors[i]->context << " " << errors[i]->code.message();
  }
}

TEST(ConcurrencySmokeTest, ParallelPipelines) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  constexpr int kThreads = 4;
  std::vector<std::thread> threads;
  std::vector<std::optional<Error>> errors(kThreads);

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i] {
      Command first(helper);
      first.arg("--stdout-bytes").arg(std::to_string(256 + i * 32));
      Command second(helper);
      second.arg("--echo-stdin");

      auto output = (first | second).output();
      if (!output) {
        errors[i] = output.error();
        return;
      }
      if (output->stdout_data.size() != static_cast<std::size_t>(256 + i * 32)) {
        errors[i] = Error{.code = make_error_code(errc::read_failed), .context = "size"};
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  for (int i = 0; i < kThreads; ++i) {
    ASSERT_FALSE(errors[i].has_value()) << errors[i]->context << " " << errors[i]->code.message();
  }
}

}  // namespace procly
