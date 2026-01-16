#pragma once

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <optional>
#include <utility>

#include "procly/platform.hpp"
#include "procly/result.hpp"

namespace procly::internal {

class unique_fd {
 public:
  unique_fd() = default;
  explicit unique_fd(int fd) : fd_(fd) {}
  unique_fd(unique_fd&& other) noexcept : fd_(other.release()) {}
  unique_fd& operator=(unique_fd&& other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }
  unique_fd(const unique_fd&) = delete;
  unique_fd& operator=(const unique_fd&) = delete;
  ~unique_fd() { reset(-1); }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] int release() noexcept {
    int fd = fd_;
    fd_ = -1;
    return fd;
  }

  void reset(int fd) noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = fd;
  }

  explicit operator bool() const noexcept { return fd_ >= 0; }

 private:
  int fd_{-1};
};

inline Result<void> set_cloexec(int fd) {
  int flags = ::fcntl(fd, F_GETFD);
  if (flags == -1) {
    return Error{std::error_code(errno, std::system_category()), "fcntl(F_GETFD)"};
  }
  if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
    return Error{std::error_code(errno, std::system_category()), "fcntl(F_SETFD)"};
  }
  return {};
}

inline Result<void> set_nonblocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL);
  if (flags == -1) {
    return Error{std::error_code(errno, std::system_category()), "fcntl(F_GETFL)"};
  }
  if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return Error{std::error_code(errno, std::system_category()), "fcntl(F_SETFL)"};
  }
  return {};
}

inline Result<std::pair<unique_fd, unique_fd>> create_pipe() {
#if PROCLY_PLATFORM_LINUX
  std::array<int, 2> fds{};
  if (::pipe2(fds.data(), O_CLOEXEC) == -1) {
    return Error{std::error_code(errno, std::system_category()), "pipe2"};
  }
  return std::make_pair(unique_fd(fds[0]), unique_fd(fds[1]));
#else
  std::array<int, 2> fds{};
  if (::pipe(fds.data()) == -1) {
    return Error{std::error_code(errno, std::system_category()), "pipe"};
  }
  auto cloexec0 = set_cloexec(fds[0]);
  if (!cloexec0) {
    return cloexec0.error();
  }
  auto cloexec1 = set_cloexec(fds[1]);
  if (!cloexec1) {
    return cloexec1.error();
  }
  return std::make_pair(unique_fd(fds[0]), unique_fd(fds[1]));
#endif
}

}  // namespace procly::internal
