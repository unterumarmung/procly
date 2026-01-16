#include "procly/internal/lowering.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>

namespace procly {

namespace {

bool env_contains(const std::vector<std::string>& envp, const std::string& key,
                  const std::string& value) {
  std::string match = key + "=" + value;
  for (const auto& entry : envp) {
    if (entry == match) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(LoweringTest, EmptyArgvIsError) {
  Command cmd("");
  auto& argv = const_cast<std::vector<std::string>&>(internal::CommandAccess::argv(cmd));
  argv.clear();
  auto result = internal::lower_command(cmd, internal::SpawnMode::spawn, nullptr);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, make_error_code(errc::empty_argv));
}

TEST(LoweringTest, OutputModeDefaultsToPiped) {
  Command cmd("echo");
  auto result = internal::lower_command(cmd, internal::SpawnMode::output, nullptr);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->stdout_spec.kind, internal::StdioSpec::Kind::piped);
  EXPECT_EQ(result->stderr_spec.kind, internal::StdioSpec::Kind::piped);
}

TEST(LoweringTest, ArgsPointerSizeAppends) {
  Command cmd("echo");
  std::array<std::string, 2> extra{{"one", "two"}};
  cmd.args(extra.data(), extra.size());

  const auto& argv = internal::CommandAccess::argv(cmd);
  ASSERT_EQ(argv.size(), 3u);
  EXPECT_EQ(argv[1], "one");
  EXPECT_EQ(argv[2], "two");
}

TEST(LoweringTest, MergeStderrDuplicatesStdout) {
  Command cmd("echo");
  SpawnOptions opts;
  opts.merge_stderr_into_stdout = true;
  cmd.options(opts);
  auto result = internal::lower_command(cmd, internal::SpawnMode::output, nullptr);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->stderr_spec.kind, internal::StdioSpec::Kind::dup_stdout);
}

TEST(LoweringTest, FileSpecDefaultsByStream) {
  Command cmd("echo");
  cmd.stdin(Stdio::file("/tmp/procly_stdin"));
  cmd.stdout(Stdio::file("/tmp/procly_stdout"));
  cmd.stderr(Stdio::file("/tmp/procly_stderr"));
  auto result = internal::lower_command(cmd, internal::SpawnMode::spawn, nullptr);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->stdin_spec.mode, OpenMode::read);
  EXPECT_EQ(result->stdout_spec.mode, OpenMode::write_truncate);
  EXPECT_EQ(result->stderr_spec.mode, OpenMode::write_truncate);
}

TEST(LoweringTest, FileSpecRejectsNonReadableStdin) {
  Command cmd("echo");
  cmd.stdin(Stdio::file("/tmp/procly_stdin", OpenMode::write_append));
  auto result = internal::lower_command(cmd, internal::SpawnMode::spawn, nullptr);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, make_error_code(errc::invalid_stdio));
}

TEST(LoweringTest, FileSpecRejectsNonWritableStdout) {
  Command cmd("echo");
  cmd.stdout(Stdio::file("/tmp/procly_stdout", OpenMode::read));
  auto result = internal::lower_command(cmd, internal::SpawnMode::spawn, nullptr);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, make_error_code(errc::invalid_stdio));
}

TEST(LoweringTest, FdSpecRejectsNegativeFd) {
  Command cmd("echo");
  cmd.stdin(Stdio::fd(-1));
  auto result = internal::lower_command(cmd, internal::SpawnMode::spawn, nullptr);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, make_error_code(errc::invalid_stdio));
}

TEST(LoweringTest, FileSpecReadWriteAllowed) {
  Command cmd("echo");
  cmd.stdin(Stdio::file("/tmp/procly_stdin", OpenMode::read_write));
  cmd.stdout(Stdio::file("/tmp/procly_stdout", OpenMode::read_write));
  auto result = internal::lower_command(cmd, internal::SpawnMode::spawn, nullptr);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->stdin_spec.mode, OpenMode::read_write);
  EXPECT_EQ(result->stdout_spec.mode, OpenMode::read_write);
}

TEST(LoweringTest, EnvironmentClearAndOverride) {
  ::setenv("PROCLY_TEST_ENV", "one", 1);
  Command cmd("echo");
  cmd.env_clear();
  cmd.env("PROCLY_TEST_ENV", "two");
  auto result = internal::lower_command(cmd, internal::SpawnMode::spawn, nullptr);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(env_contains(result->envp, "PROCLY_TEST_ENV", "two"));
  EXPECT_FALSE(env_contains(result->envp, "PROCLY_TEST_ENV", "one"));
}

TEST(LoweringTest, EnvironmentRemoveKey) {
  ::setenv("PROCLY_TEST_ENV_REMOVE", "one", 1);
  Command cmd("echo");
  cmd.env_remove("PROCLY_TEST_ENV_REMOVE");
  auto result = internal::lower_command(cmd, internal::SpawnMode::spawn, nullptr);
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(env_contains(result->envp, "PROCLY_TEST_ENV_REMOVE", "one"));
}

TEST(LoweringTest, PipelineEmptyIsError) {
  Pipeline pipeline;
  auto result = internal::lower_pipeline(pipeline, internal::SpawnMode::spawn);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, make_error_code(errc::invalid_pipeline));
}

TEST(LoweringTest, PipelineWiringAndModes) {
  Command first("echo");
  Command second("cat");
  Pipeline pipeline = first | second;

  auto result = internal::lower_pipeline(pipeline, internal::SpawnMode::output);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->stages.size(), 2u);

  const auto& stage0 = result->stages[0];
  const auto& stage1 = result->stages[1];
  EXPECT_FALSE(stage0.stdin_from_prev);
  EXPECT_TRUE(stage0.stdout_to_next);
  EXPECT_TRUE(stage1.stdin_from_prev);
  EXPECT_FALSE(stage1.stdout_to_next);
  EXPECT_EQ(stage0.mode, internal::SpawnMode::spawn);
  EXPECT_EQ(stage1.mode, internal::SpawnMode::output);
}

TEST(LoweringTest, PipelineStdioOverridesOnlyAffectEnds) {
  Command first("echo");
  Command second("cat");
  Pipeline pipeline = first | second;
  pipeline.stdin(Stdio::null());
  pipeline.stdout(Stdio::null());
  pipeline.stderr(Stdio::null());

  auto result = internal::lower_pipeline(pipeline, internal::SpawnMode::spawn);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->stages.size(), 2u);

  const auto& stage0 = result->stages[0];
  const auto& stage1 = result->stages[1];
  EXPECT_TRUE(stage0.overrides.stdin_override.has_value());
  EXPECT_FALSE(stage1.overrides.stdin_override.has_value());
  EXPECT_FALSE(stage0.overrides.stdout_override.has_value());
  EXPECT_TRUE(stage1.overrides.stdout_override.has_value());
  EXPECT_FALSE(stage0.overrides.stderr_override.has_value());
  EXPECT_TRUE(stage1.overrides.stderr_override.has_value());
}

}  // namespace procly
