#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>

#include "procly/platform.hpp"
#if PROCLY_PLATFORM_POSIX
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#if PROCLY_PLATFORM_POSIX && defined(PROCLY_FORCE_FORK)
#include <dlfcn.h>
#endif
#if PROCLY_PLATFORM_WINDOWS
#include <process.h>
#endif

#include "procly/child.hpp"
#include "procly/command.hpp"
#include "procly/internal/lowering.hpp"
#include "procly/internal/posix_spawn.hpp"
#include "procly/pipeline.hpp"
#include "tests/helpers/runfiles_support.hpp"

#if PROCLY_PLATFORM_POSIX && defined(PROCLY_FORCE_FORK)
namespace {

std::atomic<bool> g_fail_env_after_fork{false};
std::atomic<bool> g_capture_fork_result{false};
std::atomic<bool> g_open_extra_fd_before_fork{false};
std::atomic<bool> g_use_high_injected_fd{false};
std::atomic<bool> g_force_child_open_max_override{false};
std::atomic<bool> g_in_postfork_child{false};
std::atomic<int> g_last_injected_fd{-1};
std::atomic<pid_t> g_last_fork_child{-1};

using clearenv_fn = int (*)();
using setenv_fn = int (*)(const char*, const char*, int);
using unsetenv_fn = int (*)(const char*);
using fork_fn = pid_t (*)();
using sysconf_fn = long (*)(int);

clearenv_fn g_real_clearenv = nullptr;
setenv_fn g_real_setenv = nullptr;
unsetenv_fn g_real_unsetenv = nullptr;
fork_fn g_real_fork = nullptr;
sysconf_fn g_real_sysconf = nullptr;

constexpr int kInjectedFdLowerBound = 200;
constexpr long kChildOpenMaxOverride = 3;

void resolve_env_fns() {
  if (!g_real_clearenv) {
    g_real_clearenv = reinterpret_cast<clearenv_fn>(::dlsym(RTLD_NEXT, "clearenv"));
  }
  if (!g_real_setenv) {
    g_real_setenv = reinterpret_cast<setenv_fn>(::dlsym(RTLD_NEXT, "setenv"));
  }
  if (!g_real_unsetenv) {
    g_real_unsetenv = reinterpret_cast<unsetenv_fn>(::dlsym(RTLD_NEXT, "unsetenv"));
  }
}

void resolve_fork_fn() {
  if (!g_real_fork) {
    g_real_fork = reinterpret_cast<fork_fn>(::dlsym(RTLD_NEXT, "fork"));
  }
}

void resolve_sysconf_fn() {
  if (!g_real_sysconf) {
    g_real_sysconf = reinterpret_cast<sysconf_fn>(::dlsym(RTLD_NEXT, "sysconf"));
  }
}

}  // namespace

extern "C" int clearenv() {
  if (g_fail_env_after_fork.load(std::memory_order_relaxed)) {
    errno = EPERM;
    return -1;
  }
  if (!g_real_clearenv) {
    resolve_env_fns();
  }
  if (g_real_clearenv) {
    return g_real_clearenv();
  }
  errno = ENOSYS;
  return -1;
}

extern "C" int setenv(const char* name, const char* value, int overwrite) {
  if (g_fail_env_after_fork.load(std::memory_order_relaxed)) {
    errno = EPERM;
    return -1;
  }
  if (!g_real_setenv) {
    resolve_env_fns();
  }
  if (g_real_setenv) {
    return g_real_setenv(name, value, overwrite);
  }
  errno = ENOSYS;
  return -1;
}

extern "C" int unsetenv(const char* name) {
  if (g_fail_env_after_fork.load(std::memory_order_relaxed)) {
    errno = EPERM;
    return -1;
  }
  if (!g_real_unsetenv) {
    resolve_env_fns();
  }
  if (g_real_unsetenv) {
    return g_real_unsetenv(name);
  }
  errno = ENOSYS;
  return -1;
}

extern "C" long sysconf(int name) {
  // Keep this override narrow: only force _SC_OPEN_MAX in the post-fork child.
  if (name == _SC_OPEN_MAX && g_force_child_open_max_override.load(std::memory_order_relaxed) &&
      g_in_postfork_child.load(std::memory_order_relaxed)) {
    return kChildOpenMaxOverride;
  }

  if (!g_real_sysconf) {
    resolve_sysconf_fn();
  }
  if (g_real_sysconf) {
    return g_real_sysconf(name);
  }
  errno = ENOSYS;
  return -1;
}

extern "C" pid_t fork() {
  if (!g_real_fork) {
    resolve_fork_fn();
  }
  if (!g_real_fork) {
    errno = ENOSYS;
    return -1;
  }

  int injected_fd = -1;
  if (g_open_extra_fd_before_fork.load(std::memory_order_relaxed)) {
    injected_fd = ::open("/dev/null", O_RDONLY);
    if (injected_fd >= 0 && g_use_high_injected_fd.load(std::memory_order_relaxed)) {
      // Pin the injected descriptor into a high range so leak detection is deterministic.
      int high_fd = ::fcntl(injected_fd, F_DUPFD, kInjectedFdLowerBound);
      if (high_fd >= 0) {
        ::close(injected_fd);
        injected_fd = high_fd;
      }
    }
  }
  g_last_injected_fd.store(injected_fd, std::memory_order_relaxed);

  pid_t pid = g_real_fork();
  g_in_postfork_child.store(pid == 0, std::memory_order_relaxed);
  if (pid > 0 && g_capture_fork_result.load(std::memory_order_relaxed)) {
    g_last_fork_child.store(pid, std::memory_order_relaxed);
  }
  if (pid != 0 && injected_fd >= 0) {
    ::close(injected_fd);
  }
  return pid;
}
#endif

namespace procly {

namespace {

int procly_test_pid() {
#if PROCLY_PLATFORM_WINDOWS
  return _getpid();
#else
  return ::getpid();
#endif
}

std::filesystem::path unique_temp_path(std::string_view stem) {
  static std::atomic<std::uint64_t> counter{0};
  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  auto tid = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
  auto id = counter.fetch_add(1, std::memory_order_relaxed);

  std::string filename = "procly_";
  filename.append(stem.data(), stem.size());
  filename.push_back('_');
  filename.append(std::to_string(procly_test_pid()));
  filename.push_back('_');
  filename.append(std::to_string(static_cast<std::uint64_t>(now)));
  filename.push_back('_');
  filename.append(std::to_string(tid));
  filename.push_back('_');
  filename.append(std::to_string(id));
  filename.append(".txt");

  return std::filesystem::temp_directory_path() / filename;
}

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

#if PROCLY_PLATFORM_POSIX && defined(PROCLY_FORCE_FORK)
class ScopedEnvFailure {
 public:
  ScopedEnvFailure() {
    resolve_env_fns();
    g_fail_env_after_fork.store(true, std::memory_order_relaxed);
  }
  ~ScopedEnvFailure() { g_fail_env_after_fork.store(false, std::memory_order_relaxed); }
};

class ScopedForkCapture {
 public:
  explicit ScopedForkCapture(bool inject_fd, bool use_high_injected_fd = false)
      : previous_capture_(g_capture_fork_result.exchange(true, std::memory_order_relaxed)),
        previous_inject_(
            g_open_extra_fd_before_fork.exchange(inject_fd, std::memory_order_relaxed)),
        previous_use_high_(
            g_use_high_injected_fd.exchange(use_high_injected_fd, std::memory_order_relaxed)) {
    resolve_fork_fn();
    g_last_fork_child.store(-1, std::memory_order_relaxed);
    g_last_injected_fd.store(-1, std::memory_order_relaxed);
  }

  ~ScopedForkCapture() {
    g_use_high_injected_fd.store(previous_use_high_, std::memory_order_relaxed);
    g_open_extra_fd_before_fork.store(previous_inject_, std::memory_order_relaxed);
    g_capture_fork_result.store(previous_capture_, std::memory_order_relaxed);
  }

  pid_t last_child_pid() const { return g_last_fork_child.load(std::memory_order_relaxed); }
  int last_injected_fd() const { return g_last_injected_fd.load(std::memory_order_relaxed); }

 private:
  bool previous_capture_;
  bool previous_inject_;
  bool previous_use_high_;
};

class ScopedChildOpenMaxOverride {
 public:
  ScopedChildOpenMaxOverride()
      : previous_(g_force_child_open_max_override.exchange(true, std::memory_order_relaxed)) {
    resolve_sysconf_fn();
  }

  ~ScopedChildOpenMaxOverride() {
    g_force_child_open_max_override.store(previous_, std::memory_order_relaxed);
  }

 private:
  bool previous_;
};
#endif

#if PROCLY_PLATFORM_POSIX
class ScopedUmask {
 public:
  explicit ScopedUmask(mode_t mask) : previous_(::umask(mask)) {}
  ~ScopedUmask() { ::umask(previous_); }

 private:
  mode_t previous_;
};

std::size_t count_open_fds() {
#if PROCLY_PLATFORM_LINUX
  std::size_t count = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd", ec)) {
    (void)entry;
    ++count;
  }
  return count;
#else
  long max_fd = ::sysconf(_SC_OPEN_MAX);
  if (max_fd < 0) {
    max_fd = 256;
  }
  std::size_t count = 0;
  for (int fd = 0; fd < max_fd; ++fd) {
    errno = 0;
    if (::fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
      ++count;
    }
  }
  return count;
#endif
}

std::optional<pid_t> read_pid_file(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return std::nullopt;
  }
  long long value = 0;
  file >> value;
  if (!file) {
    return std::nullopt;
  }
  return static_cast<pid_t>(value);
}

pid_t wait_for_pid_file(const std::filesystem::path& path, std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    auto pid = read_pid_file(path);
    if (pid.has_value()) {
      return *pid;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return -1;
}

bool wait_for_process_exit(pid_t pid, std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (::kill(pid, 0) == -1) {
      if (errno == ESRCH) {
        return true;
      }
      if (errno == EPERM) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

#if PROCLY_HAS_THREAD_SANITIZER
constexpr std::chrono::milliseconds kPidFileWaitTimeout{5000};
constexpr std::chrono::milliseconds kProcessExitWaitTimeout{5000};
#else
constexpr std::chrono::milliseconds kPidFileWaitTimeout{1000};
constexpr std::chrono::milliseconds kProcessExitWaitTimeout{1000};
#endif

std::vector<int> read_fd_list(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::vector<int> fds;
  int fd = -1;
  while (file >> fd) {
    fds.push_back(fd);
  }
  return fds;
}

void close_non_stdio_fds() {
  long max_fd = ::sysconf(_SC_OPEN_MAX);
  if (max_fd < 0) {
    max_fd = 256;
  }
  for (int fd = 3; fd < max_fd; ++fd) {
    ::close(fd);
  }
}

// Baseline the helper's "normal" fd set on this host (including sanitizer/runtime fds).
// What: capture the helper's open fds after an exec with only stdio inherited.
// Where: used by NoFdLeakIntoGrandchild below.
// When: run per-test to avoid stale assumptions across different sanitizer builds.
// Why: macOS sanitizer runtimes keep extra fds open even when our spawn code is correct.
std::vector<int> baseline_helper_fds(const std::string& helper) {
  std::filesystem::path fd_path = unique_temp_path("baseline_fds");
  std::error_code remove_ec;
  std::filesystem::remove(fd_path, remove_ec);

  pid_t pid = ::fork();
  if (pid == 0) {
    close_non_stdio_fds();
    ::execl(helper.c_str(), helper.c_str(), "--write-open-fds", fd_path.c_str(),
            static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) {
    ADD_FAILURE() << "fork failed";
    return {};
  }

  int status = 0;
  if (::waitpid(pid, &status, 0) == -1) {
    ADD_FAILURE() << "waitpid failed";
  }

  auto fds = read_fd_list(fd_path);
  std::filesystem::remove(fd_path, remove_ec);

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    ADD_FAILURE() << "baseline helper failed";
  }
  return fds;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}
#endif

}  // namespace

TEST(CommandIntegrationTest, OutputCapturesStdoutAndStderr) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command cmd(helper);
  cmd.arg("--stdout-bytes").arg("5");
  cmd.arg("--stderr-bytes").arg("3");

  auto out = cmd.output();
  ASSERT_TRUE(out.has_value()) << out.error().context << " " << out.error().code.message();
  EXPECT_EQ(out->stdout_data.size(), 5u);
  EXPECT_EQ(out->stderr_data.size(), 3u);
}

TEST(CommandIntegrationTest, MergeStderrIntoStdout) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command cmd(helper);
  cmd.arg("--stdout-bytes").arg("5");
  cmd.arg("--stderr-bytes").arg("3");
  SpawnOptions opts;
  opts.merge_stderr_into_stdout = true;
  cmd.options(opts);

  auto out = cmd.output();
  ASSERT_TRUE(out.has_value()) << out.error().context << " " << out.error().code.message();
  EXPECT_EQ(out->stderr_data.size(), 0u);
  EXPECT_EQ(out->stdout_data.size(), 8u);
}

TEST(CommandIntegrationTest, StatusReturnsExitCode) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command cmd(helper);
  cmd.arg("--exit-code").arg("7");
  auto status = cmd.status();
  ASSERT_TRUE(status.has_value()) << status.error().context << " " << status.error().code.message();
  ASSERT_TRUE(status->code().has_value());
  EXPECT_EQ(status->code().value(), 7);
}

TEST(CommandIntegrationTest, CwdOverride) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::filesystem::path cwd = std::filesystem::temp_directory_path();

  Command cmd(helper);
  cmd.arg("--print-cwd");
  cmd.current_dir(cwd);
  auto output = cmd.output();
  ASSERT_TRUE(output.has_value()) << output.error().context << " " << output.error().code.message();
  std::filesystem::path reported(output->stdout_data);
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::equivalent(reported, cwd, ec)) << ec.message();
}

TEST(CommandIntegrationTest, EnvClearAndSet) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command cmd(helper);
  cmd.arg("--print-env").arg("PROCLY_ENV_TEST");
  cmd.env_clear();
#if PROCLY_HAS_UNDEFINED_BEHAVIOR_SANITIZER
  // Keep sanitizer runtime discoverable after env_clear() in UBSan builds.
  const char* ld_library_path = std::getenv("LD_LIBRARY_PATH");
  if (ld_library_path && *ld_library_path) {
    cmd.env("LD_LIBRARY_PATH", ld_library_path);
  }
#endif
  cmd.env("PROCLY_ENV_TEST", "value");
  auto output = cmd.output();
  ASSERT_TRUE(output.has_value()) << output.error().context << " " << output.error().code.message();
  EXPECT_EQ(output->stdout_data, "value");
}

TEST(CommandIntegrationTest, StdoutFileRedirection) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::filesystem::path out_path = unique_temp_path("stdout");
  std::error_code remove_ec;
  std::filesystem::remove(out_path, remove_ec);

  Command cmd(helper);
  cmd.arg("--stdout-bytes").arg("4");
  cmd.stdout(Stdio::file(out_path));
  auto status = cmd.status();
  ASSERT_TRUE(status.has_value()) << status.error().context << " " << status.error().code.message();

  std::ifstream file(out_path, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  EXPECT_EQ(data.size(), 4u);
  std::filesystem::remove(out_path, remove_ec);
}

TEST(CommandIntegrationTest, StdoutFileAppend) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::filesystem::path out_path = unique_temp_path("stdout_append");
  std::error_code remove_ec;
  std::filesystem::remove(out_path, remove_ec);

  for (int run = 0; run < 2; ++run) {
    Command cmd(helper);
    cmd.arg("--stdout-bytes").arg("4");
    cmd.stdout(Stdio::file(out_path, OpenMode::write_append));
    auto status = cmd.status();
    ASSERT_TRUE(status.has_value())
        << status.error().context << " " << status.error().code.message();
  }

  std::ifstream file(out_path, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  EXPECT_EQ(data.size(), 8u);
  std::filesystem::remove(out_path, remove_ec);
}

#if PROCLY_PLATFORM_POSIX
TEST(CommandIntegrationTest, StdoutFilePermissions) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::filesystem::path out_path = unique_temp_path("stdout_perms");
  std::error_code remove_ec;
  std::filesystem::remove(out_path, remove_ec);

  ScopedUmask umask_guard(0);
  Command cmd(helper);
  cmd.arg("--stdout-bytes").arg("1");
  cmd.stdout(Stdio::file(out_path, OpenMode::write_truncate, static_cast<FilePerms>(0640)));
  auto status = cmd.status();
  ASSERT_TRUE(status.has_value()) << status.error().context << " " << status.error().code.message();

  struct stat st{};
  std::string out_path_str = out_path.string();
  ASSERT_EQ(::stat(out_path_str.c_str(), &st), 0);
  EXPECT_EQ(static_cast<int>(st.st_mode & 0777), 0640);

  std::filesystem::remove(out_path, remove_ec);
}
#endif

TEST(CommandIntegrationTest, StdinFileRedirection) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::filesystem::path in_path = unique_temp_path("stdin");
  std::ofstream in_file(in_path, std::ios::binary);
  in_file << "ping";
  in_file.close();

  Command cmd(helper);
  cmd.arg("--echo-stdin");
  cmd.stdin(Stdio::file(in_path));
  auto output = cmd.output();
  ASSERT_TRUE(output.has_value()) << output.error().context << " " << output.error().code.message();
  EXPECT_EQ(output->stdout_data, "ping");

  std::error_code remove_ec;
  std::filesystem::remove(in_path, remove_ec);
}

TEST(CommandIntegrationTest, NullRedirection) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command cmd(helper);
  cmd.arg("--stdout-bytes").arg("4");
  cmd.stdout(Stdio::null());
  auto status = cmd.status();
  ASSERT_TRUE(status.has_value()) << status.error().context << " " << status.error().code.message();
}

TEST(CommandIntegrationTest, WaitTimeout) {
  Command cmd("/bin/sleep");
  cmd.arg("2");
  auto child_result = cmd.spawn();
  ASSERT_TRUE(child_result.has_value())
      << child_result.error().context << " " << child_result.error().code.message();

  WaitOptions opts;
  opts.timeout = std::chrono::milliseconds(10);
  auto wait_result = child_result->wait(opts);
  ASSERT_FALSE(wait_result.has_value());
  EXPECT_EQ(wait_result.error().code, make_error_code(errc::timeout));
}

#if PROCLY_PLATFORM_POSIX && defined(PROCLY_FORCE_FORK)
TEST(CommandIntegrationTest, ForkPathClosesFdsOpenedBetweenPreparationAndFork) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  auto baseline_fds = baseline_helper_fds(helper);
  ASSERT_FALSE(baseline_fds.empty());
  std::unordered_set<int> allowed_fds(baseline_fds.begin(), baseline_fds.end());

  std::filesystem::path fd_path = unique_temp_path("fork_fd_race");
  std::error_code remove_ec;
  std::filesystem::remove(fd_path, remove_ec);

  {
    ScopedForkCapture fork_capture(/*inject_fd=*/true);
    Command cmd(helper);
    cmd.arg("--write-open-fds").arg(fd_path.string());
    cmd.stdin(Stdio::null());
    cmd.stdout(Stdio::null());
    cmd.stderr(Stdio::null());

    auto status = cmd.status();
    ASSERT_TRUE(status.has_value())
        << status.error().context << " " << status.error().code.message();
  }

  auto fds = read_fd_list(fd_path);
  ASSERT_FALSE(fds.empty());
  for (int fd : fds) {
    EXPECT_TRUE(allowed_fds.find(fd) != allowed_fds.end());
  }
  std::filesystem::remove(fd_path, remove_ec);
}

TEST(CommandIntegrationTest, ForkPathFdCleanupDoesNotDependOnChildSysconf) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::filesystem::path fd_path = unique_temp_path("fork_fd_race_sysconf");
  std::error_code remove_ec;
  std::filesystem::remove(fd_path, remove_ec);

  int injected_fd = -1;
  {
    // Force an injected high descriptor and a tiny _SC_OPEN_MAX in the post-fork child.
    // If procly computes the close bound in child via sysconf, the injected descriptor leaks.
    ScopedForkCapture fork_capture(/*inject_fd=*/true, /*use_high_injected_fd=*/true);
    ScopedChildOpenMaxOverride open_max_override;

    Command cmd(helper);
    cmd.arg("--write-open-fds").arg(fd_path.string());
    cmd.stdin(Stdio::null());
    cmd.stdout(Stdio::null());
    cmd.stderr(Stdio::null());

    auto status = cmd.status();
    ASSERT_TRUE(status.has_value())
        << status.error().context << " " << status.error().code.message();

    injected_fd = fork_capture.last_injected_fd();
    ASSERT_GE(injected_fd, kInjectedFdLowerBound);
  }

  auto fds = read_fd_list(fd_path);
  ASSERT_FALSE(fds.empty());

  bool leaked_injected_fd = false;
  for (int fd : fds) {
    if (fd == injected_fd) {
      leaked_injected_fd = true;
      break;
    }
  }
  EXPECT_FALSE(leaked_injected_fd) << "injected fd leaked into child: " << injected_fd;
  std::filesystem::remove(fd_path, remove_ec);
}

TEST(CommandIntegrationTest, ForkExecFailureReapsChildBeforeReturningError) {
  ScopedForkCapture fork_capture(/*inject_fd=*/false);

  Command cmd("/definitely/missing/procly_binary");
  auto child_result = cmd.spawn();
  ASSERT_FALSE(child_result.has_value());

  pid_t child_pid = fork_capture.last_child_pid();
  ASSERT_GT(child_pid, 0);

  int status = 0;
  errno = 0;
  pid_t wait_result = ::waitpid(child_pid, &status, WNOHANG);
  EXPECT_EQ(wait_result, -1);
  EXPECT_EQ(errno, ECHILD);
}

TEST(CommandIntegrationTest, ForkPathResolvesRelativeProgramUsingChildCwd) {
  std::filesystem::path dir_path = unique_temp_path("cwd_path_dir");
  std::error_code ec;
  std::filesystem::create_directory(dir_path, ec);
  ASSERT_FALSE(ec) << ec.message();

  std::filesystem::path bin_dir = dir_path / "bin";
  std::filesystem::create_directory(bin_dir, ec);
  ASSERT_FALSE(ec) << ec.message();

  std::filesystem::path script_path = bin_dir / "procly_echo";
  {
    std::ofstream script(script_path);
    ASSERT_TRUE(script.is_open());
    script << "#!/bin/sh\n";
    script << "printf \"cwd_exec_ok\"";
  }
  ASSERT_EQ(::chmod(script_path.c_str(), 0755), 0);

  Command cmd("procly_echo");
  cmd.current_dir(dir_path);
  cmd.env_clear();
  cmd.env("PATH", "bin");
  auto out = cmd.output();
  ASSERT_TRUE(out.has_value()) << out.error().context << " " << out.error().code.message();
  EXPECT_EQ(out->stdout_data, "cwd_exec_ok");

  std::filesystem::remove(script_path, ec);
  std::filesystem::remove(bin_dir, ec);
  std::filesystem::remove(dir_path, ec);
}

TEST(CommandIntegrationTest, ForkPathAvoidsAllocationsAfterFork) {
  ScopedEnvFailure env_failure;

  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command cmd(helper);
  cmd.arg("--stdout-bytes").arg("1");
  auto spec_result = internal::lower_command(cmd, internal::SpawnMode::spawn, nullptr);
  ASSERT_TRUE(spec_result.has_value());
  EXPECT_EQ(internal::select_spawn_strategy(spec_result.value()),
            internal::SpawnStrategy::fork_exec);
  auto child_result = cmd.spawn();
  ASSERT_TRUE(child_result.has_value())
      << child_result.error().context << " " << child_result.error().code.message();

  WaitOptions opts;
#if PROCLY_HAS_THREAD_SANITIZER
  // TSan startup is substantially slower; keep timeout as a liveness guard
  // while avoiding false timeouts on healthy fork/exec paths.
  opts.timeout = std::chrono::milliseconds(2000);
  opts.kill_grace = std::chrono::milliseconds(500);
#else
  opts.timeout = std::chrono::milliseconds(100);
  opts.kill_grace = std::chrono::milliseconds(50);
#endif
  auto wait_result = child_result->wait(opts);
  ASSERT_TRUE(wait_result.has_value())
      << wait_result.error().context << " " << wait_result.error().code.message();
  EXPECT_TRUE(wait_result->success());
}
#endif

TEST(CommandIntegrationTest, TryWaitReturnsEmptyWhileRunning) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command cmd(helper);
  cmd.arg("--sleep-ms").arg("200");
  auto child_result = cmd.spawn();
  ASSERT_TRUE(child_result.has_value())
      << child_result.error().context << " " << child_result.error().code.message();

  auto try_result = child_result->try_wait();
  ASSERT_TRUE(try_result.has_value())
      << try_result.error().context << " " << try_result.error().code.message();
  if (try_result->has_value()) {
    EXPECT_TRUE(try_result->value().success());
  } else {
    auto wait_result = child_result->wait();
    ASSERT_TRUE(wait_result.has_value())
        << wait_result.error().context << " " << wait_result.error().code.message();
    EXPECT_TRUE(wait_result->success());
  }
}

TEST(CommandIntegrationTest, StdinPipeRoundTrip) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command cmd(helper);
  cmd.arg("--echo-stdin");
  cmd.stdin(Stdio::piped());
  cmd.stdout(Stdio::piped());

  auto child_result = cmd.spawn();
  ASSERT_TRUE(child_result.has_value())
      << child_result.error().context << " " << child_result.error().code.message();

  auto stdin_pipe = child_result->take_stdin();
  auto stdout_pipe = child_result->take_stdout();
  ASSERT_TRUE(stdin_pipe.has_value());
  ASSERT_TRUE(stdout_pipe.has_value());

  std::string payload = "stdin_payload";
  auto write_result = stdin_pipe->write_all(payload);
  ASSERT_TRUE(write_result.has_value());
  stdin_pipe->close();

  auto read_result = stdout_pipe->read_all();
  ASSERT_TRUE(read_result.has_value());
  EXPECT_EQ(read_result.value(), payload);

  auto wait_result = child_result->wait();
  ASSERT_TRUE(wait_result.has_value())
      << wait_result.error().context << " " << wait_result.error().code.message();
  EXPECT_TRUE(wait_result->success());
}

TEST(CommandIntegrationTest, MergeStderrIntoStdoutToFile) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::filesystem::path out_path = unique_temp_path("merge_file");
  std::error_code remove_ec;
  std::filesystem::remove(out_path, remove_ec);

  Command cmd(helper);
  cmd.arg("--stdout-bytes").arg("5");
  cmd.arg("--stderr-bytes").arg("3");
  cmd.stdout(Stdio::file(out_path));

  SpawnOptions opts;
  opts.merge_stderr_into_stdout = true;
  cmd.options(opts);

  auto status = cmd.status();
  ASSERT_TRUE(status.has_value()) << status.error().context << " " << status.error().code.message();

  std::string data = read_file(out_path);
  EXPECT_EQ(data.size(), 8u);
  std::size_t a_count = 0;
  std::size_t b_count = 0;
  for (char ch : data) {
    if (ch == 'a') {
      ++a_count;
    } else if (ch == 'b') {
      ++b_count;
    }
  }
  EXPECT_EQ(a_count, 5u);
  EXPECT_EQ(b_count, 3u);
  std::filesystem::remove(out_path, remove_ec);
}

TEST(PipelineIntegrationTest, OutputCapturesLastStage) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command producer(helper);
  producer.arg("--stdout-bytes").arg("4");

  Command consumer(helper);
  consumer.arg("--echo-stdin");

  Pipeline pipeline = producer | consumer;
  auto output = pipeline.output();
  ASSERT_TRUE(output.has_value()) << output.error().context << " " << output.error().code.message();
  EXPECT_EQ(output->stdout_data.size(), 4u);
}

TEST(PipelineIntegrationTest, PipefailReportsFirstFailure) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command bad(helper);
  bad.arg("--exit-code").arg("5");

  Command good(helper);

  Pipeline pipeline = bad | good;
  pipeline.pipefail(true);
  auto status = pipeline.status();
  ASSERT_TRUE(status.has_value()) << status.error().context << " " << status.error().code.message();
  ASSERT_TRUE(status->code().has_value());
  EXPECT_EQ(status->code().value(), 5);
}

TEST(PipelineIntegrationTest, DefaultPipefailUsesLastStage) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command bad(helper);
  bad.arg("--exit-code").arg("5");

  Command good(helper);
  good.arg("--exit-code").arg("0");

  Pipeline pipeline = bad | good;
  auto status = pipeline.status();
  ASSERT_TRUE(status.has_value()) << status.error().context << " " << status.error().code.message();
  ASSERT_TRUE(status->code().has_value());
  EXPECT_EQ(status->code().value(), 0);
}

TEST(PipelineIntegrationTest, OutputCapturesLastStageStderr) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command first(helper);

  Command second(helper);
  second.arg("--stderr-bytes").arg("3");

  Pipeline pipeline = first | second;
  auto output = pipeline.output();
  ASSERT_TRUE(output.has_value()) << output.error().context << " " << output.error().code.message();
  EXPECT_EQ(output->stderr_data.size(), 3u);
}

TEST(CommandIntegrationTest, OutputLargePayloads) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  constexpr std::size_t kStdoutBytes = 8 * 1024 * 1024;
  constexpr std::size_t kStderrBytes = 4 * 1024 * 1024;

  Command cmd(helper);
  cmd.arg("--stdout-bytes").arg(std::to_string(kStdoutBytes));
  cmd.arg("--stderr-bytes").arg(std::to_string(kStderrBytes));

  auto out = cmd.output();
  ASSERT_TRUE(out.has_value()) << out.error().context << " " << out.error().code.message();
  EXPECT_EQ(out->stdout_data.size(), kStdoutBytes);
  EXPECT_EQ(out->stderr_data.size(), kStderrBytes);
}

TEST(CommandIntegrationTest, OutputParallelCalls) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  constexpr int kThreads = 8;
  std::vector<std::thread> threads;
  std::vector<std::optional<Output>> outputs(kThreads);
  std::vector<std::optional<Error>> errors(kThreads);

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i] {
      std::size_t stdout_bytes = 256 + static_cast<std::size_t>(i) * 32;
      std::size_t stderr_bytes = 64 + static_cast<std::size_t>(i) * 16;
      Command cmd(helper);
      cmd.arg("--stdout-bytes").arg(std::to_string(stdout_bytes));
      cmd.arg("--stderr-bytes").arg(std::to_string(stderr_bytes));
      auto out = cmd.output();
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
    std::size_t stdout_bytes = 256 + static_cast<std::size_t>(i) * 32;
    std::size_t stderr_bytes = 64 + static_cast<std::size_t>(i) * 16;
    EXPECT_EQ(outputs[i]->stdout_data.size(), stdout_bytes);
    EXPECT_EQ(outputs[i]->stderr_data.size(), stderr_bytes);
  }
}

TEST(PipelineIntegrationTest, StdinStdoutRoundTrip) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command first(helper);
  first.arg("--echo-stdin");

  Command second(helper);
  second.arg("--echo-stdin");

  Pipeline pipeline = first | second;
  pipeline.stdin(Stdio::piped());
  pipeline.stdout(Stdio::piped());

  auto child_result = pipeline.spawn();
  ASSERT_TRUE(child_result.has_value())
      << child_result.error().context << " " << child_result.error().code.message();

  auto stdin_pipe = child_result->take_stdin();
  auto stdout_pipe = child_result->take_stdout();
  ASSERT_TRUE(stdin_pipe.has_value());
  ASSERT_TRUE(stdout_pipe.has_value());

  std::string payload = "pipeline_ping";
  auto write_result = stdin_pipe->write_all(payload);
  ASSERT_TRUE(write_result.has_value());
  stdin_pipe->close();

  auto read_result = stdout_pipe->read_all();
  ASSERT_TRUE(read_result.has_value());
  EXPECT_EQ(read_result.value(), payload);

  auto wait_result = child_result->wait();
  ASSERT_TRUE(wait_result.has_value())
      << wait_result.error().context << " " << wait_result.error().code.message();
  EXPECT_TRUE(wait_result->aggregate.success());
}

TEST(PipelineIntegrationTest, StderrPipedFromLastStage) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  Command first(helper);

  Command second(helper);
  second.arg("--stderr-bytes").arg("6");

  Pipeline pipeline = first | second;
  pipeline.stderr(Stdio::piped());

  auto child_result = pipeline.spawn();
  ASSERT_TRUE(child_result.has_value())
      << child_result.error().context << " " << child_result.error().code.message();

  auto stderr_pipe = child_result->take_stderr();
  ASSERT_TRUE(stderr_pipe.has_value());

  auto read_result = stderr_pipe->read_all();
  ASSERT_TRUE(read_result.has_value());
  EXPECT_EQ(read_result->size(), 6u);

  auto wait_result = child_result->wait();
  ASSERT_TRUE(wait_result.has_value())
      << wait_result.error().context << " " << wait_result.error().code.message();
  EXPECT_TRUE(wait_result->aggregate.success());
}

#if PROCLY_PLATFORM_POSIX
TEST(PipelineIntegrationTest, TerminateKillsGrandchildInProcessGroup) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::filesystem::path pid_path = unique_temp_path("grandchild_pid");
  std::error_code remove_ec;
  std::filesystem::remove(pid_path, remove_ec);

  Command first(helper);
  first.arg("--spawn-grandchild");
  first.arg("--grandchild-pid-file").arg(pid_path.string());
  first.arg("--grandchild-sleep-ms").arg("5000");
  first.arg("--consume-stdin");

  Command second(helper);
  second.arg("--consume-stdin");

  Pipeline pipeline = first | second;
  pipeline.new_process_group(true);
  pipeline.stdin(Stdio::piped());

  auto child_result = pipeline.spawn();
  ASSERT_TRUE(child_result.has_value())
      << child_result.error().context << " " << child_result.error().code.message();

  auto stdin_pipe = child_result->take_stdin();
  ASSERT_TRUE(stdin_pipe.has_value());

  pid_t grandchild_pid = wait_for_pid_file(pid_path, kPidFileWaitTimeout);
  ASSERT_GT(grandchild_pid, 0);

  auto term_result = child_result->terminate();
  ASSERT_TRUE(term_result.has_value())
      << term_result.error().context << " " << term_result.error().code.message();

  auto wait_result = child_result->wait();
  ASSERT_TRUE(wait_result.has_value())
      << wait_result.error().context << " " << wait_result.error().code.message();

  bool exited = wait_for_process_exit(grandchild_pid, kProcessExitWaitTimeout);
  if (!exited) {
    ::kill(grandchild_pid, SIGKILL);
  }
  EXPECT_TRUE(exited);

  stdin_pipe->close();
  std::filesystem::remove(pid_path, remove_ec);
}

TEST(CommandIntegrationTest, FdCountStableAfterRepeatedStatus) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::size_t before = count_open_fds();
  for (int i = 0; i < 50; ++i) {
    Command cmd(helper);
    cmd.arg("--stdout-bytes").arg("1");
    auto status = cmd.status();
    ASSERT_TRUE(status.has_value())
        << status.error().context << " " << status.error().code.message();
  }
  std::size_t after = count_open_fds();
  EXPECT_EQ(after, before);
}

TEST(CommandIntegrationTest, FdCountStableAfterRepeatedOutput) {
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::size_t before = count_open_fds();
  for (int i = 0; i < 50; ++i) {
    Command cmd(helper);
    cmd.arg("--stdout-bytes").arg("4");
    cmd.arg("--stderr-bytes").arg("2");
    auto out = cmd.output();
    ASSERT_TRUE(out.has_value()) << out.error().context << " " << out.error().code.message();
  }
  std::size_t after = count_open_fds();
  EXPECT_EQ(after, before);
}

TEST(CommandIntegrationTest, NoFdLeakIntoGrandchild) {
#if PROCLY_HAS_THREAD_SANITIZER
  GTEST_SKIP() << "TSan runtime keeps extra file descriptors open in child processes.";
#endif
  std::string helper = helper_path();
  ASSERT_FALSE(helper.empty());

  std::filesystem::path fd_path = unique_temp_path("grandchild_fds");
  std::error_code remove_ec;
  std::filesystem::remove(fd_path, remove_ec);

  Command cmd(helper);
  cmd.arg("--spawn-grandchild");
  cmd.arg("--grandchild-write-open-fds").arg(fd_path.string());
  cmd.stdin(Stdio::null());
  cmd.stdout(Stdio::null());
  cmd.stderr(Stdio::null());

  auto status = cmd.status();
  ASSERT_TRUE(status.has_value()) << status.error().context << " " << status.error().code.message();

  auto fds = read_fd_list(fd_path);
  ASSERT_FALSE(fds.empty());
  auto baseline_fds = baseline_helper_fds(helper);
  ASSERT_FALSE(baseline_fds.empty());
  std::unordered_set<int> allowed_fds(baseline_fds.begin(), baseline_fds.end());
  for (int fd : fds) {
    EXPECT_TRUE(allowed_fds.find(fd) != allowed_fds.end());
  }

  std::filesystem::remove(fd_path, remove_ec);
}
#endif

}  // namespace procly
