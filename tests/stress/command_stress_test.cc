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

TEST(CommandStressTest, RepeatedLargeOutput) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  constexpr int kRuns = 100;
  constexpr std::size_t kStdoutBytes = 256 * 1024;
  constexpr std::size_t kStderrBytes = 128 * 1024;

  for (int i = 0; i < kRuns; ++i) {
    Command cmd(helper);
    cmd.arg("--stdout-bytes").arg(std::to_string(kStdoutBytes));
    cmd.arg("--stderr-bytes").arg(std::to_string(kStderrBytes));
    auto out = cmd.output();
    ASSERT_TRUE(out.has_value()) << out.error().context << " " << out.error().code.message();
    EXPECT_EQ(out->stdout_data.size(), kStdoutBytes);
    EXPECT_EQ(out->stderr_data.size(), kStderrBytes);
  }
}

TEST(CommandStressTest, ParallelPipelines) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  constexpr int kThreads = 8;
  std::vector<std::thread> threads;
  std::vector<std::optional<Output>> outputs(kThreads);
  std::vector<std::optional<Error>> errors(kThreads);

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i] {
      Command first(helper);
      first.arg("--stdout-bytes").arg(std::to_string(1024 + i * 128));
      Command second(helper);
      second.arg("--echo-stdin");
      Pipeline pipeline = first | second;
      auto out = pipeline.output();
      if (out.has_value()) {
        outputs[i] = std::move(out.value());
      } else {
        errors[i] = out.error();
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  for (int i = 0; i < kThreads; ++i) {
    if (errors[i].has_value()) {
      ADD_FAILURE() << errors[i]->context << " " << errors[i]->code.message();
      continue;
    }
    ASSERT_TRUE(outputs[i].has_value());
    EXPECT_EQ(outputs[i]->stdout_data.size(), static_cast<std::size_t>(1024 + i * 128));
  }
}

TEST(CommandStressTest, RepeatedTerminate) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  constexpr int kRuns = 50;
  for (int i = 0; i < kRuns; ++i) {
    Command cmd(helper);
    cmd.arg("--sleep-ms").arg("1000");
    auto child_result = cmd.spawn();
    ASSERT_TRUE(child_result.has_value())
        << child_result.error().context << " " << child_result.error().code.message();

    auto term_result = child_result->terminate();
    ASSERT_TRUE(term_result.has_value())
        << term_result.error().context << " " << term_result.error().code.message();

    auto wait_result = child_result->wait();
    ASSERT_TRUE(wait_result.has_value())
        << wait_result.error().context << " " << wait_result.error().code.message();
  }
}

}  // namespace procly
