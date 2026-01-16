#include "procly/internal/io_drain.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>

#include "procly/internal/fd.hpp"
#include "procly/pipe.hpp"

namespace procly {

TEST(IoDrainTest, DrainsStdoutAndStderr) {
  auto out_pipe = internal::create_pipe();
  ASSERT_TRUE(out_pipe.has_value());
  auto err_pipe = internal::create_pipe();
  ASSERT_TRUE(err_pipe.has_value());

  PipeReader out_reader(out_pipe->first.release());
  PipeWriter out_writer(out_pipe->second.release());
  PipeReader err_reader(err_pipe->first.release());
  PipeWriter err_writer(err_pipe->second.release());

  std::string out_payload(16384, 'o');
  std::string err_payload(8192, 'e');

  std::atomic<bool> out_ok{true};
  std::atomic<bool> err_ok{true};
  std::thread out_thread([&] {
    out_ok = out_writer.write_all(out_payload).has_value();
    out_writer.close();
  });
  std::thread err_thread([&] {
    err_ok = err_writer.write_all(err_payload).has_value();
    err_writer.close();
  });

  auto drained = internal::drain_pipes(&out_reader, &err_reader);
  ASSERT_TRUE(drained.has_value());
  EXPECT_EQ(drained->stdout_data, out_payload);
  EXPECT_EQ(drained->stderr_data, err_payload);

  out_thread.join();
  err_thread.join();
  EXPECT_TRUE(out_ok);
  EXPECT_TRUE(err_ok);
}

TEST(IoDrainTest, DrainsSinglePipe) {
  auto out_pipe = internal::create_pipe();
  ASSERT_TRUE(out_pipe.has_value());

  PipeReader out_reader(out_pipe->first.release());
  PipeWriter out_writer(out_pipe->second.release());

  std::string out_payload(4096, 'x');
  std::atomic<bool> out_ok{true};
  std::thread out_thread([&] {
    out_ok = out_writer.write_all(out_payload).has_value();
    out_writer.close();
  });

  auto drained = internal::drain_pipes(&out_reader, nullptr);
  ASSERT_TRUE(drained.has_value());
  EXPECT_EQ(drained->stdout_data, out_payload);
  EXPECT_TRUE(drained->stderr_data.empty());

  out_thread.join();
  EXPECT_TRUE(out_ok);
}

TEST(IoDrainTest, HandlesLargePayloads) {
  auto out_pipe = internal::create_pipe();
  ASSERT_TRUE(out_pipe.has_value());
  auto err_pipe = internal::create_pipe();
  ASSERT_TRUE(err_pipe.has_value());

  PipeReader out_reader(out_pipe->first.release());
  PipeWriter out_writer(out_pipe->second.release());
  PipeReader err_reader(err_pipe->first.release());
  PipeWriter err_writer(err_pipe->second.release());

  std::string out_payload(1 * 1024 * 1024, 'a');
  std::string err_payload(512 * 1024, 'b');

  std::atomic<bool> out_ok{true};
  std::atomic<bool> err_ok{true};
  std::thread out_thread([&] {
    out_ok = out_writer.write_all(out_payload).has_value();
    out_writer.close();
  });
  std::thread err_thread([&] {
    err_ok = err_writer.write_all(err_payload).has_value();
    err_writer.close();
  });

  auto drained = internal::drain_pipes(&out_reader, &err_reader);
  ASSERT_TRUE(drained.has_value());
  EXPECT_EQ(drained->stdout_data.size(), out_payload.size());
  EXPECT_EQ(drained->stderr_data.size(), err_payload.size());

  out_thread.join();
  err_thread.join();
  EXPECT_TRUE(out_ok);
  EXPECT_TRUE(err_ok);
}

}  // namespace procly
