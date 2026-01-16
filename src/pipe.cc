#include "procly/pipe.hpp"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

#include "procly/result.hpp"

namespace procly {

namespace {

Error make_errno_error(const char* context) {
  return Error{std::error_code(errno, std::system_category()), context};
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
    return Error{make_error_code(errc::invalid_stdio), "read"};
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
    return Error{make_error_code(errc::invalid_stdio), "read"};
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
    return Error{make_error_code(errc::invalid_stdio), "write"};
  }
  while (true) {
    ssize_t rv = ::write(fd_, data, n);
    if (rv >= 0) {
      return static_cast<std::size_t>(rv);
    }
    if (errno == EINTR) {
      continue;
    }
    return make_errno_error("write");
  }
}

Result<void> PipeWriter::write_all(std::string_view data) const {
  if (fd_ < 0) {
    return Error{make_error_code(errc::invalid_stdio), "write"};
  }
  std::size_t offset = 0;
  while (offset < data.size()) {
    auto write_result = write_some(data.data() + offset, data.size() - offset);
    if (!write_result) {
      return write_result.error();
    }
    if (write_result.value() == 0) {
      return Error{make_error_code(errc::write_failed), "write"};
    }
    offset += write_result.value();
  }
  return {};
}

}  // namespace procly
