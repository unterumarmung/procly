#include "procly/internal/posix_spawn.hpp"

#include <gtest/gtest.h>
#include <spawn.h>

#include <filesystem>

#include "procly/internal/backend.hpp"

namespace procly::internal {

TEST(PosixSpawnTest, CanUsePosixSpawnWithoutCwd) {
  SpawnSpec spec;
  spec.argv = {"echo"};
  EXPECT_TRUE(can_use_posix_spawn(spec));
}

TEST(PosixSpawnTest, CwdRequiresSupport) {
  SpawnSpec spec;
  spec.argv = {"echo"};
  spec.cwd = std::filesystem::current_path();
#if PROCLY_PLATFORM_MACOS
  EXPECT_TRUE(can_use_posix_spawn(spec));
#else
  EXPECT_FALSE(can_use_posix_spawn(spec));
#endif
}

TEST(PosixSpawnTest, ProcessGroupRequiresSupport) {
  SpawnSpec spec;
  spec.argv = {"echo"};
  spec.opts.new_process_group = true;
#ifdef POSIX_SPAWN_SETPGROUP
  EXPECT_TRUE(can_use_posix_spawn(spec));
#else
  EXPECT_FALSE(can_use_posix_spawn(spec));
#endif
}

}  // namespace procly::internal
