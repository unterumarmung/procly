#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "procly/platform.hpp"

namespace {

using std::string_literals::operator""s;

constexpr int kParseBase = 10;
constexpr std::size_t kIoBufferSize = 4096;
constexpr int kDefaultGrandchildSleepMs = 1000;
constexpr long kFallbackMaxFd = 256;

struct Options {
  std::size_t stdout_bytes = 0;
  std::size_t stderr_bytes = 0;
  std::optional<int> exit_code;
  std::optional<int> sleep_ms;
  std::optional<int> grandchild_sleep_ms;
  std::optional<std::string> write_open_fds;
  std::optional<std::string> grandchild_write_open_fds;
  std::optional<std::string> grandchild_pid_file;
  bool echo_stdin = false;
  bool consume_stdin = false;
  bool spawn_grandchild = false;
  std::optional<std::string> print_env;
  bool print_cwd = false;
};

bool parse_size(const std::string& value, std::size_t* out) {
  char* end = nullptr;
  errno = 0;
  unsigned long long parsed = std::strtoull(value.c_str(), &end, kParseBase);
  if (errno != 0 || end == value.c_str() || *end != '\0') {
    return false;
  }
  *out = static_cast<std::size_t>(parsed);
  return true;
}

bool parse_int(const std::string& value, int* out) {
  char* end = nullptr;
  errno = 0;
  long parsed = std::strtol(value.c_str(), &end, kParseBase);
  if (errno != 0 || end == value.c_str() || *end != '\0') {
    return false;
  }
  *out = static_cast<int>(parsed);
  return true;
}

bool parse_args(int argc, char* argv[], Options* options) {  // NOLINT(modernize-avoid-c-arrays)
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--stdout-bytes" && i + 1 < argc) {
      if (!parse_size(argv[++i], &options->stdout_bytes)) {
        return false;
      }
      continue;
    }
    if (arg == "--stderr-bytes" && i + 1 < argc) {
      if (!parse_size(argv[++i], &options->stderr_bytes)) {
        return false;
      }
      continue;
    }
    if (arg == "--exit-code" && i + 1 < argc) {
      int value = 0;
      if (!parse_int(argv[++i], &value)) {
        return false;
      }
      options->exit_code = value;
      continue;
    }
    if (arg == "--sleep-ms" && i + 1 < argc) {
      int value = 0;
      if (!parse_int(argv[++i], &value)) {
        return false;
      }
      options->sleep_ms = value;
      continue;
    }
    if (arg == "--grandchild-sleep-ms" && i + 1 < argc) {
      int value = 0;
      if (!parse_int(argv[++i], &value)) {
        return false;
      }
      options->grandchild_sleep_ms = value;
      continue;
    }
    if (arg == "--grandchild-pid-file" && i + 1 < argc) {
      options->grandchild_pid_file = argv[++i];
      continue;
    }
    if (arg == "--write-open-fds" && i + 1 < argc) {
      options->write_open_fds = argv[++i];
      continue;
    }
    if (arg == "--grandchild-write-open-fds" && i + 1 < argc) {
      options->grandchild_write_open_fds = argv[++i];
      continue;
    }
    if (arg == "--echo-stdin") {
      options->echo_stdin = true;
      continue;
    }
    if (arg == "--consume-stdin") {
      options->consume_stdin = true;
      continue;
    }
    if (arg == "--spawn-grandchild") {
      options->spawn_grandchild = true;
      continue;
    }
    if (arg == "--print-env" && i + 1 < argc) {
      options->print_env = argv[++i];
      continue;
    }
    if (arg == "--print-cwd") {
      options->print_cwd = true;
      continue;
    }
    return false;
  }
  return true;
}

struct WriteSpec {
  std::size_t count;
  char fill;
};

void write_bytes(std::ostream& stream, WriteSpec spec) {
  std::string buffer(kIoBufferSize, spec.fill);
  std::size_t remaining = spec.count;
  while (remaining > 0) {
    std::size_t amount = remaining < buffer.size() ? remaining : buffer.size();
    stream.write(buffer.data(), static_cast<std::streamsize>(amount));
    remaining -= amount;
  }
}

void echo_stdin() {
  std::array<char, kIoBufferSize> buffer{};
  while (true) {
    ssize_t count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
    if (count > 0) {
      ssize_t offset = 0;
      while (offset < count) {
        ssize_t written = ::write(STDOUT_FILENO, buffer.data() + offset, count - offset);
        if (written > 0) {
          offset += written;
        } else if (written == -1 && errno == EINTR) {
          continue;
        } else {
          return;
        }
      }
      continue;
    }
    if (count == 0) {
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    return;
  }
}

void consume_stdin() {
  std::array<char, kIoBufferSize> buffer{};
  while (true) {
    ssize_t count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
    if (count > 0) {
      continue;
    }
    if (count == 0) {
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    return;
  }
}

void write_pid_file(const std::string& path, pid_t pid) {
  std::ofstream file(path);
  if (file) {
    file << pid;
    file.flush();
  }
}

std::vector<int> list_open_fds() {
#if PROCLY_PLATFORM_LINUX
  std::vector<int> fds;
  DIR* dir = ::opendir("/proc/self/fd");
  if (!dir) {
    return fds;
  }
  int dir_fd = ::dirfd(dir);
  while (dirent* entry = ::readdir(dir)) {
    if (!entry->d_name || entry->d_name[0] == '.') {
      continue;
    }
    char* end = nullptr;
    long value = std::strtol(entry->d_name, &end, kParseBase);
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
  std::ranges::sort(fds);
  return fds;
#else
  std::vector<int> fds;
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
#endif
}

void write_open_fds_file(const std::string& path) {
  auto fds = list_open_fds();
  std::ofstream file(path);
  if (!file) {
    return;
  }
  for (std::size_t i = 0; i < fds.size(); ++i) {
    if (i > 0) {
      file << ' ';
    }
    file << fds[i];
  }
  file.flush();
}

}  // namespace

int main(int argc, char* argv[]) {
  Options options;
  if (!parse_args(argc, argv, &options)) {
    std::cerr << "invalid args" << '\n';
    return 2;
  }

  if (options.sleep_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(*options.sleep_ms));
  }

  if (options.spawn_grandchild) {
    pid_t pid = ::fork();
    if (pid == 0) {
      if (options.grandchild_write_open_fds) {
        std::array<std::string, 3> args_storage = {
            std::string{argv[0]},
            "--write-open-fds"s,
            *options.grandchild_write_open_fds,
        };
        std::array<char*, 4> args = {
            args_storage[0].data(),
            args_storage[1].data(),
            args_storage[2].data(),
            nullptr,
        };

        ::execv(args[0], args.data());
        std::cerr << "exec failed" << '\n';
        return 1;
      }
      int sleep_ms = options.grandchild_sleep_ms.value_or(kDefaultGrandchildSleepMs);
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
      return 0;
    }
    if (pid > 0 && options.grandchild_pid_file) {
      write_pid_file(*options.grandchild_pid_file, pid);
    }
    if (pid > 0 && options.grandchild_write_open_fds) {
      int status = 0;
      ::waitpid(pid, &status, 0);
    }
    if (pid < 0) {
      std::cerr << "fork failed" << '\n';
      return 1;
    }
  }

  if (options.echo_stdin) {
    echo_stdin();
  }
  if (options.consume_stdin) {
    consume_stdin();
  }

  if (options.stdout_bytes > 0) {
    write_bytes(std::cout, WriteSpec{.count = options.stdout_bytes, .fill = 'a'});
  }

  if (options.stderr_bytes > 0) {
    write_bytes(std::cerr, WriteSpec{.count = options.stderr_bytes, .fill = 'b'});
  }

  if (options.print_env) {
    const char* value = std::getenv(options.print_env->c_str());
    if (value) {
      std::cout << value;
    }
  }

  if (options.print_cwd) {
    std::array<char, kIoBufferSize> buffer{};
    if (::getcwd(buffer.data(), buffer.size())) {
      std::cout << buffer.data();
    }
  }

  if (options.write_open_fds) {
    write_open_fds_file(*options.write_open_fds);
  }

  std::cout.flush();
  std::cerr.flush();

  return options.exit_code.value_or(0);
}
