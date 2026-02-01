#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "procly/command.hpp"
#include "procly/result.hpp"
#include "procly/status.hpp"
#include "procly/stdio.hpp"

namespace procly::internal {

struct StdioSpec {
  enum class Kind : std::uint8_t { inherit, null, piped, fd, file, dup_stdout };

  Kind kind = Kind::inherit;
  int fd = -1;
  std::filesystem::path path;
  OpenMode mode = OpenMode::read;
#if PROCLY_PLATFORM_POSIX
  std::optional<FilePerms> perms;
#endif
};

struct SpawnSpec {
  std::vector<std::string> argv;
  std::optional<std::filesystem::path> cwd;
  std::vector<std::string> envp;

  StdioSpec stdin_spec;
  StdioSpec stdout_spec;
  StdioSpec stderr_spec;

  SpawnOptions opts;
  std::optional<int> process_group;
};

struct Spawned {
  int pid = -1;
  std::optional<int> pgid;
  std::optional<int> stdin_fd;
  std::optional<int> stdout_fd;
  std::optional<int> stderr_fd;
  bool new_process_group = false;
};

class Backend {
 public:
  virtual ~Backend() = default;
  virtual Result<Spawned> spawn(const SpawnSpec& spec) = 0;
  virtual Result<ExitStatus> wait(Spawned& spawned,
                                  std::optional<std::chrono::milliseconds> timeout,
                                  std::chrono::milliseconds kill_grace) = 0;
  virtual Result<std::optional<ExitStatus>> try_wait(Spawned& spawned) = 0;
  virtual Result<void> terminate(Spawned& spawned) = 0;
  virtual Result<void> kill(Spawned& spawned) = 0;
  virtual Result<void> signal(Spawned& spawned, int signo) = 0;
};

class ScopedBackendOverride {
 public:
  explicit ScopedBackendOverride(Backend& backend);
  ~ScopedBackendOverride();
  ScopedBackendOverride(const ScopedBackendOverride&) = delete;
  ScopedBackendOverride& operator=(const ScopedBackendOverride&) = delete;

 private:
  Backend* previous_ = nullptr;
};

Backend& default_backend();

}  // namespace procly::internal
