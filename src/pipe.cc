#include "procly/pipe.hpp"

#include <pthread.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>

#include "procly/result.hpp"

namespace procly {

namespace {

Error make_errno_error(const char* context) {
  return Error{.code = std::error_code(errno, std::system_category()), .context = context};
}

Error make_pthread_error(int error, const char* context) {
  return Error{.code = std::error_code(error, std::system_category()), .context = context};
}

Result<std::size_t> write_without_sigpipe(int fd, const void* data, std::size_t n) {
  sigset_t sigpipe_set;
  sigemptyset(&sigpipe_set);
  sigaddset(&sigpipe_set, SIGPIPE);

  sigset_t previous_mask;
  int block_error = ::pthread_sigmask(SIG_BLOCK, &sigpipe_set, &previous_mask);
  if (block_error != 0) {
    return make_pthread_error(block_error, "pthread_sigmask");
  }

  sigset_t pending_before;
  if (::sigpending(&pending_before) == -1) {
    int saved_errno = errno;
    (void)::pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
    errno = saved_errno;
    return make_errno_error("sigpending");
  }
  const bool sigpipe_was_pending = sigismember(&pending_before, SIGPIPE) == 1;

  ssize_t rv = -1;
  int saved_errno = 0;
  while (true) {
    rv = ::write(fd, data, n);
    if (rv >= 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    saved_errno = errno;
    break;
  }

  if (rv == -1 && saved_errno == EPIPE && !sigpipe_was_pending) {
    sigset_t pending_after;
    if (::sigpending(&pending_after) == -1) {
      int pending_errno = errno;
      int restore_errno = ::pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
      if (restore_errno != 0) {
        return make_pthread_error(restore_errno, "pthread_sigmask");
      }
      errno = pending_errno;
      return make_errno_error("sigpending");
    }
    if (sigismember(&pending_after, SIGPIPE) == 1) {
      int received_signal = 0;
      int wait_error = ::sigwait(&sigpipe_set, &received_signal);
      if (wait_error != 0) {
        int restore_errno = ::pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
        if (restore_errno != 0) {
          return make_pthread_error(restore_errno, "pthread_sigmask");
        }
        return make_pthread_error(wait_error, "sigwait");
      }
    }
  }

  int restore_errno = ::pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
  if (rv >= 0) {
    if (restore_errno != 0) {
      return make_pthread_error(restore_errno, "pthread_sigmask");
    }
    return static_cast<std::size_t>(rv);
  }

  if (restore_errno != 0) {
    return make_pthread_error(restore_errno, "pthread_sigmask");
  }
  errno = saved_errno;
  return make_errno_error("write");
}

}  // namespace

PipeReader::PipeReader(PipeReader&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

PipeReader& PipeReader::operator=(PipeReader&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

PipeReader::~PipeReader() { close(); }

void PipeReader::close() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

#if PROCLY_HAS_STD_SPAN
Result<std::size_t> PipeReader::read_some(std::span<std::byte> buffer) const {
  return read_some(buffer.data(), buffer.size());
}
#endif

Result<std::size_t> PipeReader::read_some(void* data, std::size_t n) const {
  if (fd_ < 0) {
    return Error{.code = make_error_code(errc::invalid_stdio), .context = "read"};
  }
  while (true) {
    ssize_t rv = ::read(fd_, data, n);
    if (rv >= 0) {
      return static_cast<std::size_t>(rv);
    }
    if (errno == EINTR) {
      continue;
    }
    return make_errno_error("read");
  }
}

Result<std::string> PipeReader::read_all() const {
  if (fd_ < 0) {
    return Error{.code = make_error_code(errc::invalid_stdio), .context = "read"};
  }
  std::string out;
  constexpr std::size_t kPipeBufferSize = 8192;
  std::array<char, kPipeBufferSize> buffer{};
  while (true) {
    auto read_result = read_some(buffer.data(), buffer.size());
    if (!read_result) {
      return read_result.error();
    }
    std::size_t count = read_result.value();
    if (count == 0) {
      break;
    }
    out.append(buffer.data(), count);
  }
  return out;
}

PipeWriter::PipeWriter(PipeWriter&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

PipeWriter& PipeWriter::operator=(PipeWriter&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

PipeWriter::~PipeWriter() { close(); }

void PipeWriter::close() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

#if PROCLY_HAS_STD_SPAN
Result<std::size_t> PipeWriter::write_some(std::span<const std::byte> buffer) const {
  return write_some(buffer.data(), buffer.size());
}
#endif

Result<std::size_t> PipeWriter::write_some(const void* data, std::size_t n) const {
  if (fd_ < 0) {
    return Error{.code = make_error_code(errc::invalid_stdio), .context = "write"};
  }
  return write_without_sigpipe(fd_, data, n);
}

Result<void> PipeWriter::write_all(std::string_view data) const {
  if (fd_ < 0) {
    return Error{.code = make_error_code(errc::invalid_stdio), .context = "write"};
  }
  std::size_t offset = 0;
  while (offset < data.size()) {
    auto write_result = write_some(data.data() + offset, data.size() - offset);
    if (!write_result) {
      return write_result.error();
    }
    if (write_result.value() == 0) {
      return Error{.code = make_error_code(errc::write_failed), .context = "write"};
    }
    offset += write_result.value();
  }
  return {};
}

}  // namespace procly
