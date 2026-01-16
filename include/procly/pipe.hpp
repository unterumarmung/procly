#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "procly/platform.hpp"
#include "procly/result.hpp"

#if PROCLY_HAS_STD_SPAN
#include <span>
#endif

namespace procly {

/// @brief Read end of a pipe owned by procly.
class PipeReader {
 public:
  /// @brief Construct an empty reader.
  PipeReader() = default;
  /// @brief Construct from a native file descriptor.
  explicit PipeReader(int fd) : fd_(fd) {}
  /// @brief Move-construct a reader.
  PipeReader(PipeReader&& other) noexcept;
  /// @brief Move-assign a reader.
  PipeReader& operator=(PipeReader&& other) noexcept;
  PipeReader(const PipeReader&) = delete;
  PipeReader& operator=(const PipeReader&) = delete;
  /// @brief Destroy the reader and close if needed.
  ~PipeReader();

  /// @brief Native file descriptor handle.
  [[nodiscard]] int native_handle() const noexcept { return fd_; }
  /// @brief Close the pipe.
  void close() noexcept;

  /// @brief Read all bytes until EOF.
  [[nodiscard]] Result<std::string> read_all() const;
#if PROCLY_HAS_STD_SPAN
  /// @brief Read up to buffer.size() bytes.
  [[nodiscard]] Result<std::size_t> read_some(std::span<std::byte> buffer) const;
#endif
  /// @brief Read up to n bytes into data.
  [[nodiscard]] Result<std::size_t> read_some(void* data, std::size_t n) const;

 private:
  /// @brief Native file descriptor, or -1 if empty.
  int fd_{-1};
};

/// @brief Write end of a pipe owned by procly.
class PipeWriter {
 public:
  /// @brief Construct an empty writer.
  PipeWriter() = default;
  /// @brief Construct from a native file descriptor.
  explicit PipeWriter(int fd) : fd_(fd) {}
  /// @brief Move-construct a writer.
  PipeWriter(PipeWriter&& other) noexcept;
  /// @brief Move-assign a writer.
  PipeWriter& operator=(PipeWriter&& other) noexcept;
  PipeWriter(const PipeWriter&) = delete;
  PipeWriter& operator=(const PipeWriter&) = delete;
  /// @brief Destroy the writer and close if needed.
  ~PipeWriter();

  /// @brief Native file descriptor handle.
  [[nodiscard]] int native_handle() const noexcept { return fd_; }
  /// @brief Close the pipe.
  void close() noexcept;

  /// @brief Write all data to the pipe.
  [[nodiscard]] Result<void> write_all(std::string_view data) const;
#if PROCLY_HAS_STD_SPAN
  /// @brief Write up to buffer.size() bytes.
  [[nodiscard]] Result<std::size_t> write_some(std::span<const std::byte> buffer) const;
#endif
  /// @brief Write up to n bytes from data.
  [[nodiscard]] Result<std::size_t> write_some(const void* data, std::size_t n) const;

 private:
  /// @brief Native file descriptor, or -1 if empty.
  int fd_{-1};
};

}  // namespace procly
