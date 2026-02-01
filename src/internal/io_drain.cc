#include "procly/internal/io_drain.hpp"

#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>

#include "procly/internal/fd.hpp"

namespace procly::internal {

namespace {

Error make_errno_error(const char* context) {
  return Error{.code = std::error_code(errno, std::system_category()), .context = context};
}

}  // namespace

Result<DrainResult> drain_pipes(PipeReader* stdout_pipe, PipeReader* stderr_pipe) {
  DrainResult result;
  constexpr std::size_t kBufferSize = 8192;

  struct Target {
    PipeReader* pipe;
    std::string* out;
    bool done = false;
  };

  std::array targets = {
      Target{.pipe = stdout_pipe, .out = &result.stdout_data, .done = false},
      Target{.pipe = stderr_pipe, .out = &result.stderr_data, .done = false},
  };

  int active = 0;
  for (auto& target : targets) {
    if (target.pipe != nullptr && target.pipe->native_handle() >= 0) {
      ++active;
      auto nonblocking_result = set_nonblocking(target.pipe->native_handle());
      if (!nonblocking_result) {
        return nonblocking_result.error();
      }
    } else {
      target.done = true;
    }
  }

  std::array<pollfd, 2> pollfds{};

  while (active > 0) {
    // Poll until at least one pipe becomes readable or hits EOF.
    int poll_count = 0;
    for (const auto& target : targets) {
      if (target.done) {
        continue;
      }
      pollfds[poll_count].fd = target.pipe->native_handle();
      pollfds[poll_count].events = POLLIN;
      pollfds[poll_count].revents = 0;
      ++poll_count;
    }

    int poll_result = ::poll(pollfds.data(), poll_count, -1);
    if (poll_result == -1) {
      if (errno == EINTR) {
        continue;
      }
      return make_errno_error("poll");
    }

    int poll_index = 0;
    for (auto& target : targets) {
      if (target.done) {
        continue;
      }
      auto& pfd = pollfds[poll_index++];
      if ((pfd.revents & (POLLIN | POLLHUP)) != 0) {
        std::array<char, kBufferSize> buffer{};
        while (true) {
          ssize_t count = ::read(pfd.fd, buffer.data(), buffer.size());
          if (count > 0) {
            target.out->append(buffer.data(), static_cast<std::size_t>(count));
            continue;
          }
          if (count == 0) {
            target.pipe->close();
            target.done = true;
            --active;
            break;
          }
          if (errno == EINTR) {
            continue;
          }
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          }
          return make_errno_error("read");
        }
      }
    }
  }

  return result;
}

}  // namespace procly::internal
