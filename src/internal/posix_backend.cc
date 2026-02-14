#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <unordered_set>

#include "procly/internal/backend.hpp"
#include "procly/internal/fd.hpp"
#include "procly/internal/posix_spawn.hpp"
#include "procly/internal/wait_policy.hpp"

extern char** environ;

namespace procly::internal {

namespace {

Error make_errno_error(const char* context) {
  return Error{.code = std::error_code(errno, std::system_category()), .context = context};
}

Error make_spawn_error(int error, const char* context) {
  return Error{.code = std::error_code(error, std::system_category()), .context = context};
}

constexpr long kFallbackMaxFd = 256;
constexpr int kExecFailureExitCode = 127;
constexpr int kDefaultFileMode = 0666;

std::vector<int> list_open_fds() {
  std::vector<int> fds;
#if PROCLY_PLATFORM_LINUX
  if (DIR* dir = ::opendir("/proc/self/fd")) {
    int dir_fd = ::dirfd(dir);
    while (dirent* entry = ::readdir(dir)) {
      if (!entry->d_name || entry->d_name[0] == '.') {
        continue;
      }
      char* end = nullptr;
      long value = std::strtol(entry->d_name, &end, 10);
      if (!end || *end != '\0') {
        continue;
      }
      int fd = static_cast<int>(value);
      if (fd == dir_fd) {
        continue;
      }
      fds.push_back(fd);
    }
    ::closedir(dir);
    std::sort(fds.begin(), fds.end());
    return fds;
  }
#endif
  long max_fd = ::sysconf(_SC_OPEN_MAX);
  if (max_fd < 0) {
    max_fd = kFallbackMaxFd;
  }
  for (int fd = 0; fd < max_fd; ++fd) {
    errno = 0;
    if (::fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
      fds.push_back(fd);
    }
  }
  std::ranges::sort(fds);
  return fds;
}

Result<void> add_close_actions_for_inherited_fds(posix_spawn_file_actions_t* actions,
                                                 std::unordered_set<int>* closed_fds) {
  auto fds = list_open_fds();
  for (int fd : fds) {
    if (fd <= STDERR_FILENO) {
      continue;
    }
    if (closed_fds->contains(fd)) {
      continue;
    }
    int rc = posix_spawn_file_actions_addclose(actions, fd);
    if (rc != 0) {
      return make_spawn_error(rc, "posix_spawn_file_actions_addclose");
    }
    closed_fds->insert(fd);
  }
  return {};
}

std::optional<std::string> find_env_value(const std::vector<std::string>& envp, const char* key) {
  std::size_t key_len = std::strlen(key);
  for (const auto& entry : envp) {
    if (entry.size() <= key_len) {
      continue;
    }
    if (entry.compare(0, key_len, key) != 0 || entry[key_len] != '=') {
      continue;
    }
    return entry.substr(key_len + 1);
  }
  return std::nullopt;
}

std::filesystem::path resolve_search_dir(std::string_view raw_dir,
                                         const std::optional<std::filesystem::path>& cwd) {
  std::filesystem::path dir =
      raw_dir.empty() ? std::filesystem::path(".") : std::filesystem::path(raw_dir);
  if (cwd && dir.is_relative()) {
    return *cwd / dir;
  }
  return dir;
}

// Resolve argv[0] before fork so the child only needs async-signal-safe syscalls.
std::string resolve_exec_path(const std::string& argv0, const std::vector<std::string>& envp,
                              const std::optional<std::filesystem::path>& cwd) {
  if (argv0.find('/') != std::string::npos) {
    return argv0;
  }
  std::string path_value;
  if (auto env_path = find_env_value(envp, "PATH")) {
    path_value = std::move(*env_path);
  } else {
    path_value = "/usr/bin:/bin";
  }
  if (path_value.empty()) {
    return argv0;
  }
  std::size_t start = 0;
  while (true) {
    std::size_t end = path_value.find(':', start);
    std::size_t len = (end == std::string::npos) ? path_value.size() - start : end - start;
    std::string_view dir =
        (len == 0) ? std::string_view(".") : std::string_view(path_value).substr(start, len);
    std::filesystem::path candidate = resolve_search_dir(dir, cwd) / argv0;
    if (::access(candidate.c_str(), X_OK) == 0) {
      return candidate.string();
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return argv0;
}

long max_open_fd_limit() {
  long max_fd = ::sysconf(_SC_OPEN_MAX);
  if (max_fd < 0) {
    return kFallbackMaxFd;
  }
  return max_fd;
}

// Close all inherited descriptors after dup2 so descriptors opened by other threads between
// pre-fork bookkeeping and fork() do not leak into the exec'ed process.
void close_inherited_fds_after_fork(int keep_fd) {
  long max_fd = max_open_fd_limit();
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) {
    if (fd == keep_fd) {
      continue;
    }
    ::close(fd);
  }
}

void reap_child_after_exec_failure(pid_t pid) {
  if (pid <= 0) {
    return;
  }
  int status = 0;
  while (::waitpid(pid, &status, 0) == -1) {
    if (errno != EINTR) {
      break;
    }
  }
}

Result<int> open_null(bool read_only) {
  int flags = read_only ? O_RDONLY : O_WRONLY;
#if defined(O_CLOEXEC)
  flags |= O_CLOEXEC;
#endif
  int fd = ::open("/dev/null", flags);
  if (fd == -1) {
    return make_errno_error("open(/dev/null)");
  }
  return fd;
}

int open_flags_for(OpenMode mode) {
  switch (mode) {
    case OpenMode::read:
      return O_RDONLY;
    case OpenMode::write_truncate:
      return O_WRONLY | O_CREAT | O_TRUNC;
    case OpenMode::write_append:
      return O_WRONLY | O_CREAT | O_APPEND;
    case OpenMode::read_write:
      return O_RDWR | O_CREAT;
  }
  return O_RDONLY;
}

Result<int> open_file(const std::filesystem::path& path, OpenMode mode,
                      std::optional<FilePerms> perms) {
  int flags = open_flags_for(mode);
#if defined(O_CLOEXEC)
  flags |= O_CLOEXEC;
#endif
  constexpr int kFileMode = 0666;
  int fd = ::open(path.c_str(), flags,
                  static_cast<int>(perms.value_or(static_cast<FilePerms>(kFileMode))));
  if (fd == -1) {
    return make_errno_error("open(file)");
  }
  return fd;
}

ExitStatus to_exit_status(int status) {
  if (WIFEXITED(status)) {
    return ExitStatus::exited(WEXITSTATUS(status), static_cast<std::uint32_t>(status));
  }
  return ExitStatus::other(static_cast<std::uint32_t>(status));
}

Result<ExitStatus> wait_pid(pid_t pid, int options) {
  int status = 0;
  while (true) {
    pid_t rv = ::waitpid(pid, &status, options);
    if (rv == pid) {
      return to_exit_status(status);
    }
    if (rv == 0) {
      return ExitStatus::other(0);
    }
    if (errno == EINTR) {
      continue;
    }
    return make_errno_error("waitpid");
  }
}

Result<void> send_signal(const Spawned& spawned, int signo) {
  int target = spawned.pid;
  if (spawned.new_process_group && spawned.pgid) {
    target = -(*spawned.pgid);
  }
  if (::kill(target, signo) == -1) {
    return make_errno_error("kill");
  }
  return {};
}

struct SpawnActionState {
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attr;
  bool actions_ready = false;
  bool attr_ready = false;

  ~SpawnActionState() {
    if (actions_ready) {
      posix_spawn_file_actions_destroy(&actions);
    }
    if (attr_ready) {
      posix_spawnattr_destroy(&attr);
    }
  }
};

Result<void> add_spawn_action(int rc, const char* context) {
  if (rc != 0) {
    return make_spawn_error(rc, context);
  }
  return {};
}

Result<Spawned> spawn_posix_spawnp(const SpawnSpec& spec) {
  SpawnActionState state;
  std::unordered_set<int> closed_fds;
  auto init_actions = add_spawn_action(posix_spawn_file_actions_init(&state.actions),
                                       "posix_spawn_file_actions_init");
  if (!init_actions) {
    return init_actions.error();
  }
  state.actions_ready = true;

  auto init_attr = add_spawn_action(posix_spawnattr_init(&state.attr), "posix_spawnattr_init");
  if (!init_attr) {
    return init_attr.error();
  }
  state.attr_ready = true;

  std::vector<int> opened_fds;
  std::optional<int> parent_stdin;
  std::optional<int> parent_stdout;
  std::optional<int> parent_stderr;
  auto cleanup_and_return = [&](const Error& error) -> Result<Spawned> {
    for (int fd : opened_fds) {
      ::close(fd);
    }
    return error;
  };

  auto add_close = [&](int fd) -> Result<void> {
    if (fd < 0) {
      return {};
    }
    if (closed_fds.contains(fd)) {
      return {};
    }
    auto rc = posix_spawn_file_actions_addclose(&state.actions, fd);
    if (rc != 0) {
      return make_spawn_error(rc, "posix_spawn_file_actions_addclose");
    }
    closed_fds.insert(fd);
    return {};
  };
  auto add_dup = [&](int src_fd, int dst_fd) -> Result<void> {
    return add_spawn_action(posix_spawn_file_actions_adddup2(&state.actions, src_fd, dst_fd),
                            "posix_spawn_file_actions_adddup2");
  };
  auto add_open = [&](int dst_fd, const char* path, int flags, int mode) -> Result<void> {
    return add_spawn_action(
        posix_spawn_file_actions_addopen(&state.actions, dst_fd, path, flags, mode),
        "posix_spawn_file_actions_addopen");
  };

  if (spec.cwd) {
#if PROCLY_PLATFORM_MACOS
    auto chdir_action =
        add_spawn_action(posix_spawn_file_actions_addchdir_np(&state.actions, spec.cwd->c_str()),
                         "posix_spawn_file_actions_addchdir_np");
    if (!chdir_action) {
      return cleanup_and_return(chdir_action.error());
    }
#else
    return cleanup_and_return(
        Error{.code = make_error_code(errc::chdir_failed), .context = "posix_spawn_chdir"});
#endif
  }

  short flags = 0;
  if (spec.opts.new_process_group || spec.process_group) {
#ifdef POSIX_SPAWN_SETPGROUP
    flags = static_cast<short>(flags | POSIX_SPAWN_SETPGROUP);
    pid_t pgid = spec.opts.new_process_group ? 0 : *spec.process_group;
    auto set_pgroup =
        add_spawn_action(posix_spawnattr_setpgroup(&state.attr, pgid), "posix_spawnattr_setpgroup");
    if (!set_pgroup) {
      return cleanup_and_return(set_pgroup.error());
    }
#else
    return cleanup_and_return(
        Error{.code = make_error_code(errc::spawn_failed), .context = "posix_spawn_pgroup"});
#endif
  }
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
  flags = static_cast<short>(flags | POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
  if (flags != 0) {
    auto set_flags =
        add_spawn_action(posix_spawnattr_setflags(&state.attr, flags), "posix_spawnattr_setflags");
    if (!set_flags) {
      return cleanup_and_return(set_flags.error());
    }
  }

  auto setup_stdio = [&](const StdioSpec& stdio, int target_fd, bool read_only,
                         std::optional<int>& parent_fd) -> Result<void> {
    switch (stdio.kind) {
      case StdioSpec::Kind::inherit:
        return {};
      case StdioSpec::Kind::null: {
        int flags = read_only ? O_RDONLY : O_WRONLY;
        return add_open(target_fd, "/dev/null", flags, 0);
      }
      case StdioSpec::Kind::file: {
        int flags = open_flags_for(stdio.mode);
        int perms =
            static_cast<int>(stdio.perms.value_or(static_cast<FilePerms>(kDefaultFileMode)));
        return add_open(target_fd, stdio.path.c_str(), flags, perms);
      }
      case StdioSpec::Kind::fd: {
        if (stdio.fd == target_fd) {
          return {};
        }
        return add_dup(stdio.fd, target_fd);
      }
      case StdioSpec::Kind::piped: {
        auto pipe_result = create_pipe();
        if (!pipe_result) {
          return pipe_result.error();
        }
        auto [read_end, write_end] = std::move(pipe_result.value());
        int read_fd = read_end.release();
        int write_fd = write_end.release();
        opened_fds.push_back(read_fd);
        opened_fds.push_back(write_fd);
        if (read_only) {
          parent_fd = write_fd;
          auto dup_read = add_dup(read_fd, target_fd);
          if (!dup_read) {
            return dup_read.error();
          }
        } else {
          parent_fd = read_fd;
          auto dup_write = add_dup(write_fd, target_fd);
          if (!dup_write) {
            return dup_write.error();
          }
        }
        auto close_read = add_close(read_fd);
        if (!close_read) {
          return close_read.error();
        }
        auto close_write = add_close(write_fd);
        if (!close_write) {
          return close_write.error();
        }
        return {};
      }
      case StdioSpec::Kind::dup_stdout:
        return Error{.code = make_error_code(errc::invalid_stdio), .context = "stdio"};
    }
    return Error{.code = make_error_code(errc::invalid_stdio), .context = "stdio"};
  };

  auto stdin_result = setup_stdio(spec.stdin_spec, STDIN_FILENO, true, parent_stdin);
  if (!stdin_result) {
    return cleanup_and_return(stdin_result.error());
  }
  auto stdout_result = setup_stdio(spec.stdout_spec, STDOUT_FILENO, false, parent_stdout);
  if (!stdout_result) {
    return cleanup_and_return(stdout_result.error());
  }

  if (spec.stderr_spec.kind == StdioSpec::Kind::dup_stdout) {
    auto dup_result = add_dup(STDOUT_FILENO, STDERR_FILENO);
    if (!dup_result) {
      return cleanup_and_return(dup_result.error());
    }
  } else {
    auto stderr_result = setup_stdio(spec.stderr_spec, STDERR_FILENO, false, parent_stderr);
    if (!stderr_result) {
      return cleanup_and_return(stderr_result.error());
    }
  }

#if !defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
  auto close_result = add_close_actions_for_inherited_fds(&state.actions, &closed_fds);
  if (!close_result) {
    return cleanup_and_return(close_result.error());
  }
#endif

  std::vector<std::string> argv_copy = spec.argv;
  std::vector<char*> argv_c;
  argv_c.reserve(argv_copy.size() + 1);
  for (auto& arg : argv_copy) {
    argv_c.push_back(arg.data());
  }
  argv_c.push_back(nullptr);

  std::vector<std::string> envp_copy = spec.envp;
  std::vector<char*> envp_c;
  envp_c.reserve(envp_copy.size() + 1);
  for (auto& entry : envp_copy) {
    envp_c.push_back(entry.data());
  }
  envp_c.push_back(nullptr);

  pid_t pid = -1;
  int spawn_rc =
      ::posix_spawnp(&pid, argv_c[0], &state.actions, &state.attr, argv_c.data(), envp_c.data());
  if (spawn_rc != 0) {
    return cleanup_and_return(make_spawn_error(spawn_rc, "posix_spawnp"));
  }

  Spawned spawned;
  spawned.pid = pid;
  spawned.new_process_group = spec.opts.new_process_group || spec.process_group.has_value();
  if (spec.opts.new_process_group) {
    spawned.pgid = pid;
  } else if (spec.process_group) {
    spawned.pgid = spec.process_group;
  }
  spawned.stdin_fd = parent_stdin;
  spawned.stdout_fd = parent_stdout;
  spawned.stderr_fd = parent_stderr;

  for (int fd : opened_fds) {
    bool keep = (parent_stdin && fd == *parent_stdin) || (parent_stdout && fd == *parent_stdout) ||
                (parent_stderr && fd == *parent_stderr);
    if (!keep) {
      ::close(fd);
    }
  }

  return spawned;
}

class PosixBackend final : public Backend {
 public:
  Result<Spawned> spawn(const SpawnSpec& spec) override {
    if (spec.argv.empty()) {
      return Error{.code = make_error_code(errc::empty_argv), .context = "argv"};
    }

    if (select_spawn_strategy(spec) == SpawnStrategy::posix_spawn) {
      return spawn_posix_spawnp(spec);
    }

    std::vector<int> opened_fds;
    std::optional<int> parent_stdin;
    std::optional<int> parent_stdout;
    std::optional<int> parent_stderr;

    int child_stdin = STDIN_FILENO;
    int child_stdout = STDOUT_FILENO;
    int child_stderr = STDERR_FILENO;

    auto open_for_spec = [&](const StdioSpec& spec, bool read_only, bool is_stdout,
                             std::optional<int>& parent_fd) -> Result<int> {
      switch (spec.kind) {
        case StdioSpec::Kind::inherit:
          if (read_only) {
            return STDIN_FILENO;
          }
          if (is_stdout) {
            return STDOUT_FILENO;
          }
          return STDERR_FILENO;
        case StdioSpec::Kind::null: {
          auto fd = open_null(read_only);
          if (!fd) {
            return fd.error();
          }
          opened_fds.push_back(fd.value());
          return fd.value();
        }
        case StdioSpec::Kind::file: {
          auto fd = open_file(spec.path, spec.mode, spec.perms);
          if (!fd) {
            return fd.error();
          }
          opened_fds.push_back(fd.value());
          return fd.value();
        }
        case StdioSpec::Kind::fd:
          return spec.fd;
        case StdioSpec::Kind::piped: {
          auto pipe_result = create_pipe();
          if (!pipe_result) {
            return pipe_result.error();
          }
          auto [read_end, write_end] = std::move(pipe_result.value());
          int read_fd = read_end.release();
          int write_fd = write_end.release();
          opened_fds.push_back(read_fd);
          opened_fds.push_back(write_fd);
          if (read_only) {
            parent_fd = write_fd;
            return read_fd;
          }
          parent_fd = read_fd;
          return write_fd;
        }
        case StdioSpec::Kind::dup_stdout:
          return STDOUT_FILENO;
      }
      return Error{.code = make_error_code(errc::invalid_stdio), .context = "stdio"};
    };

    auto stdout_fd = open_for_spec(spec.stdout_spec, false, true, parent_stdout);
    if (!stdout_fd) {
      return stdout_fd.error();
    }
    child_stdout = stdout_fd.value();

    auto stdin_fd = open_for_spec(spec.stdin_spec, true, false, parent_stdin);
    if (!stdin_fd) {
      return stdin_fd.error();
    }
    child_stdin = stdin_fd.value();

    if (spec.stderr_spec.kind == StdioSpec::Kind::dup_stdout) {
      child_stderr = child_stdout;
    } else {
      auto stderr_fd = open_for_spec(spec.stderr_spec, false, false, parent_stderr);
      if (!stderr_fd) {
        return stderr_fd.error();
      }
      child_stderr = stderr_fd.value();
    }

    // Error pipe communicates child setup/exec failures back to the parent.
    auto error_pipe_result = create_pipe();
    if (!error_pipe_result) {
      return error_pipe_result.error();
    }
    auto [error_read, error_write] = std::move(error_pipe_result.value());
    int error_read_fd = error_read.release();
    int error_write_fd = error_write.release();
    opened_fds.push_back(error_read_fd);
    opened_fds.push_back(error_write_fd);

    std::vector<std::string> argv_copy = spec.argv;
    std::vector<char*> argv_c;
    argv_c.reserve(argv_copy.size() + 1);
    for (auto& arg : argv_copy) {
      argv_c.push_back(arg.data());
    }
    argv_c.push_back(nullptr);

    std::vector<std::string> envp_copy = spec.envp;
    std::vector<char*> envp_c;
    envp_c.reserve(envp_copy.size() + 1);
    for (auto& entry : envp_copy) {
      envp_c.push_back(entry.data());
    }
    envp_c.push_back(nullptr);

    std::string exec_path = resolve_exec_path(argv_copy.front(), envp_copy, spec.cwd);

    pid_t pid = ::fork();
    if (pid == -1) {
      for (int fd : opened_fds) {
        ::close(fd);
      }
      return make_errno_error("fork");
    }

    if (pid == 0) {
      if (error_read_fd >= 0) {
        ::close(error_read_fd);
      }

      if (spec.opts.new_process_group) {
        if (::setpgid(0, 0) == -1) {
          int err = errno;
          ::write(error_write_fd, &err, sizeof(err));
          _exit(kExecFailureExitCode);
        }
      } else if (spec.process_group) {
        if (::setpgid(0, *spec.process_group) == -1) {
          int err = errno;
          ::write(error_write_fd, &err, sizeof(err));
          _exit(kExecFailureExitCode);
        }
      }

      if (spec.cwd) {
        if (::chdir(spec.cwd->c_str()) == -1) {
          int err = errno;
          ::write(error_write_fd, &err, sizeof(err));
          _exit(kExecFailureExitCode);
        }
      }

      if (child_stdin != STDIN_FILENO) {
        if (::dup2(child_stdin, STDIN_FILENO) == -1) {
          int err = errno;
          ::write(error_write_fd, &err, sizeof(err));
          _exit(kExecFailureExitCode);
        }
      }
      if (child_stdout != STDOUT_FILENO) {
        if (::dup2(child_stdout, STDOUT_FILENO) == -1) {
          int err = errno;
          ::write(error_write_fd, &err, sizeof(err));
          _exit(kExecFailureExitCode);
        }
      }
      if (child_stderr != STDERR_FILENO) {
        if (::dup2(child_stderr, STDERR_FILENO) == -1) {
          int err = errno;
          ::write(error_write_fd, &err, sizeof(err));
          _exit(kExecFailureExitCode);
        }
      }

      close_inherited_fds_after_fork(error_write_fd);

      ::execve(exec_path.c_str(), argv_c.data(), envp_c.data());

      int err = errno;
      ::write(error_write_fd, &err, sizeof(err));
      _exit(kExecFailureExitCode);
    }

    ::close(error_write_fd);
    int child_errno = 0;
    ssize_t read_result = -1;
    while (read_result == -1) {
      read_result = ::read(error_read_fd, &child_errno, sizeof(child_errno));
      if (read_result == -1 && errno != EINTR) {
        break;
      }
    }
    ::close(error_read_fd);
    if (read_result == -1) {
      for (int fd : opened_fds) {
        if (fd == error_read_fd || fd == error_write_fd) {
          continue;
        }
        ::close(fd);
      }
      return make_errno_error("read");
    }
    if (read_result > 0) {
      reap_child_after_exec_failure(pid);
      for (int fd : opened_fds) {
        if (fd == error_read_fd || fd == error_write_fd) {
          continue;
        }
        ::close(fd);
      }
      return Error{.code = std::error_code(child_errno, std::system_category()),
                   .context = "spawn"};
    }

    Spawned spawned;
    spawned.pid = pid;
    spawned.new_process_group = spec.opts.new_process_group || spec.process_group.has_value();
    if (spec.opts.new_process_group) {
      spawned.pgid = pid;
    } else if (spec.process_group) {
      spawned.pgid = spec.process_group;
    }

    spawned.stdin_fd = parent_stdin;
    spawned.stdout_fd = parent_stdout;
    spawned.stderr_fd = parent_stderr;

    for (int fd : opened_fds) {
      if (fd == error_read_fd || fd == error_write_fd) {
        continue;
      }
      bool keep = (parent_stdin && fd == *parent_stdin) ||
                  (parent_stdout && fd == *parent_stdout) ||
                  (parent_stderr && fd == *parent_stderr);
      if (!keep) {
        ::close(fd);
      }
    }

    return spawned;
  }

  Result<ExitStatus> wait(Spawned& spawned, std::optional<std::chrono::milliseconds> timeout,
                          std::chrono::milliseconds kill_grace) override {
    WaitOps ops;
    ops.try_wait = [&]() { return try_wait(spawned); };
    ops.wait_blocking = [&]() { return wait_pid(spawned.pid, 0); };
    ops.terminate = [&]() { return terminate(spawned); };
    ops.kill = [&]() { return kill(spawned); };
    return wait_with_timeout(ops, default_clock(), timeout, kill_grace);
  }

  Result<std::optional<ExitStatus>> try_wait(Spawned& spawned) override {
    int status = 0;
    while (true) {
      pid_t rv = ::waitpid(spawned.pid, &status, WNOHANG);
      if (rv == spawned.pid) {
        return std::optional<ExitStatus>(to_exit_status(status));
      }
      if (rv == 0) {
        return std::optional<ExitStatus>();
      }
      if (errno == EINTR) {
        continue;
      }
      return make_errno_error("waitpid");
    }
  }

  Result<void> terminate(Spawned& spawned) override { return send_signal(spawned, SIGTERM); }

  Result<void> kill(Spawned& spawned) override { return send_signal(spawned, SIGKILL); }

  Result<void> signal(Spawned& spawned, int signo) override { return send_signal(spawned, signo); }
};

}  // namespace

namespace {

std::atomic<Backend*> g_backend_override{nullptr};

}  // namespace

ScopedBackendOverride::ScopedBackendOverride(Backend& backend)
    : previous_(g_backend_override.exchange(&backend)) {}

ScopedBackendOverride::~ScopedBackendOverride() { g_backend_override.store(previous_); }

Backend& default_backend() {
  if (auto* override_backend = g_backend_override.load()) {
    return *override_backend;
  }
  static PosixBackend backend;
  return backend;
}

}  // namespace procly::internal
