#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include "procly/internal/backend.hpp"
#include "procly/internal/fd.hpp"
#include "procly/internal/posix_spawn.hpp"
#include "procly/internal/wait_policy.hpp"

extern char** environ;

namespace procly::internal {

namespace {

Error make_errno_error(const char* context) {
  return Error{std::error_code(errno, std::system_category()), context};
}

Error make_spawn_error(int error, const char* context) {
  return Error{std::error_code(error, std::system_category()), context};
}

constexpr int kExecFailureExitCode = 127;
constexpr int kDefaultFileMode = 0666;

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

Result<void> clear_environment() {
#if PROCLY_PLATFORM_MACOS
  while (::environ && *::environ) {
    std::string entry(*::environ);
    auto pos = entry.find('=');
    if (pos == std::string::npos) {
      break;
    }
    std::string key = entry.substr(0, pos);
    if (::unsetenv(key.c_str()) != 0) {
      return make_errno_error("unsetenv");
    }
  }
  return {};
#else
  if (::clearenv() != 0) {
    return make_errno_error("clearenv");
  }
  return {};
#endif
}

Result<void> apply_env(const std::vector<std::string>& envp) {
  auto cleared = clear_environment();
  if (!cleared) {
    return cleared.error();
  }
  for (const auto& entry : envp) {
    auto pos = entry.find('=');
    if (pos == std::string::npos) {
      continue;
    }
    std::string key = entry.substr(0, pos);
    std::string value = entry.substr(pos + 1);
    if (::setenv(key.c_str(), value.c_str(), 1) != 0) {
      return make_errno_error("setenv");
    }
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
    return add_spawn_action(posix_spawn_file_actions_addclose(&state.actions, fd),
                            "posix_spawn_file_actions_addclose");
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
    return cleanup_and_return(Error{make_error_code(errc::chdir_failed), "posix_spawn_chdir"});
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
    return cleanup_and_return(Error{make_error_code(errc::spawn_failed), "posix_spawn_pgroup"});
#endif
  }
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
        return Error{make_error_code(errc::invalid_stdio), "stdio"};
    }
    return Error{make_error_code(errc::invalid_stdio), "stdio"};
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
    spawned.pgid = *spec.process_group;
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
      return Error{make_error_code(errc::empty_argv), "argv"};
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
          return read_only ? STDIN_FILENO : (is_stdout ? STDOUT_FILENO : STDERR_FILENO);
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
      return Error{make_error_code(errc::invalid_stdio), "stdio"};
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

      for (int fd : opened_fds) {
        if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO ||
            fd == error_write_fd || fd == error_read_fd) {
          continue;
        }
        ::close(fd);
      }

      // Apply requested environment before exec.
      auto env_result = apply_env(spec.envp);
      if (!env_result) {
        int err = errno;
        ::write(error_write_fd, &err, sizeof(err));
        _exit(kExecFailureExitCode);
      }

      std::vector<std::string> argv_copy = spec.argv;
      std::vector<char*> argv_c;
      argv_c.reserve(argv_copy.size() + 1);
      for (auto& arg : argv_copy) {
        argv_c.push_back(arg.data());
      }
      argv_c.push_back(nullptr);

      ::execvp(argv_c[0], argv_c.data());

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
      for (int fd : opened_fds) {
        if (fd == error_read_fd || fd == error_write_fd) {
          continue;
        }
        ::close(fd);
      }
      return Error{std::error_code(child_errno, std::system_category()), "spawn"};
    }

    Spawned spawned;
    spawned.pid = pid;
    spawned.new_process_group = spec.opts.new_process_group || spec.process_group.has_value();
    if (spec.opts.new_process_group) {
      spawned.pgid = pid;
    } else if (spec.process_group) {
      spawned.pgid = *spec.process_group;
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
