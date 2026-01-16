#pragma once

#include "procly/internal/backend.hpp"

namespace procly::internal {

enum class SpawnStrategy { fork_exec, posix_spawn };

bool can_use_posix_spawn(const SpawnSpec& spec);
SpawnStrategy select_spawn_strategy(const SpawnSpec& spec);

}  // namespace procly::internal
