#include "procly/internal/posix_spawn.hpp"

#include <spawn.h>

namespace procly::internal {

namespace {

constexpr bool kHasSpawnPgroup =
#ifdef POSIX_SPAWN_SETPGROUP
    true;
#else
    false;
#endif

constexpr bool kHasSpawnChdir =
#if PROCLY_PLATFORM_MACOS
    true;
#else
    false;
#endif

}  // namespace

bool can_use_posix_spawn(const SpawnSpec& spec) {
  if (spec.cwd && !kHasSpawnChdir) {
    return false;
  }
  if ((spec.opts.new_process_group || spec.process_group) && !kHasSpawnPgroup) {
    return false;
  }
  return true;
}

SpawnStrategy select_spawn_strategy(const SpawnSpec& spec) {
#if defined(PROCLY_FORCE_FORK)
  (void)spec;
  return SpawnStrategy::fork_exec;
#elif defined(PROCLY_FORCE_POSIX_SPAWN)
  if (can_use_posix_spawn(spec)) {
    return SpawnStrategy::posix_spawn;
  }
  return SpawnStrategy::fork_exec;
#else
  if (can_use_posix_spawn(spec)) {
    return SpawnStrategy::posix_spawn;
  }
  return SpawnStrategy::fork_exec;
#endif
}

}  // namespace procly::internal
