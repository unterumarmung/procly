#include "procly/stdio.hpp"

#include <gtest/gtest.h>

namespace procly {

TEST(StdioTest, ConstructsVariants) {
  auto inherit = Stdio::inherit();
  auto nullio = Stdio::null();
  auto piped = Stdio::piped();
  auto fd = Stdio::fd(3);
  auto file = Stdio::file("/tmp/file.txt");

  EXPECT_TRUE(std::holds_alternative<Stdio::Inherit>(inherit.value));
  EXPECT_TRUE(std::holds_alternative<Stdio::Null>(nullio.value));
  EXPECT_TRUE(std::holds_alternative<Stdio::Piped>(piped.value));
  EXPECT_TRUE(std::holds_alternative<Stdio::Fd>(fd.value));
  EXPECT_TRUE(std::holds_alternative<Stdio::File>(file.value));
}

TEST(StdioTest, FileSpecStoresMode) {
  auto file = Stdio::file("/tmp/file.txt", OpenMode::write_append);
  const auto& spec = std::get<Stdio::File>(file.value);
  ASSERT_TRUE(spec.mode.has_value());
  EXPECT_EQ(spec.mode.value(), OpenMode::write_append);
}

#if PROCLY_PLATFORM_POSIX
TEST(StdioTest, FileSpecStoresPerms) {
  auto file = Stdio::file("/tmp/file.txt", OpenMode::write_truncate, static_cast<FilePerms>(0640));
  const auto& spec = std::get<Stdio::File>(file.value);
  ASSERT_TRUE(spec.perms.has_value());
  EXPECT_EQ(spec.perms.value(), static_cast<FilePerms>(0640));
}
#endif

}  // namespace procly
