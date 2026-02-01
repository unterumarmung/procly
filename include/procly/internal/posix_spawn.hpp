#pragma once

#include <cstdint>

#include "procly/internal/backend.hpp"

namespace procly::internal {

enum class SpawnStrategy : std::uint8_t { fork_exec, posix_spawn };

bool can_use_posix_spawn(const SpawnSpec& spec);
SpawnStrategy select_spawn_strategy(const SpawnSpec& spec);

}  // namespace procly::internal
