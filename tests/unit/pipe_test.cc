#include "procly/pipe.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>

#include "procly/internal/fd.hpp"

namespace procly {

TEST(PipeTest, WriteAndReadAll) {
  auto pipe_result = internal::create_pipe();
  ASSERT_TRUE(pipe_result.has_value());
  auto read_fd = pipe_result->first.release();
  auto write_fd = pipe_result->second.release();

  PipeReader reader(read_fd);
  PipeWriter writer(write_fd);

  std::string payload = "hello";
  auto write_result = writer.write_all(payload);
  ASSERT_TRUE(write_result.has_value());
  writer.close();

  auto read_result = reader.read_all();
  ASSERT_TRUE(read_result.has_value());
  EXPECT_EQ(read_result.value(), payload);
}

#if PROCLY_PLATFORM_POSIX
TEST(PipeTest, PipeFdsAreCloexec) {
  auto pipe_result = internal::create_pipe();
  ASSERT_TRUE(pipe_result.has_value());

  int read_fd = pipe_result->first.get();
  int write_fd = pipe_result->second.get();

  int read_flags = ::fcntl(read_fd, F_GETFD);
  ASSERT_NE(read_flags, -1);
  EXPECT_TRUE((read_flags & FD_CLOEXEC) != 0);

  int write_flags = ::fcntl(write_fd, F_GETFD);
  ASSERT_NE(write_flags, -1);
  EXPECT_TRUE((write_flags & FD_CLOEXEC) != 0);
}
#endif

}  // namespace procly
