#include "procly/status.hpp"

#include <gtest/gtest.h>

namespace procly {

TEST(StatusTest, ExitedSuccess) {
  auto status = ExitStatus::exited(0, 42);
  EXPECT_TRUE(status.success());
  ASSERT_TRUE(status.code().has_value());
  EXPECT_EQ(status.code().value(), 0);
  EXPECT_EQ(status.native(), 42u);
}

TEST(StatusTest, OtherHasNoCode) {
  auto status = ExitStatus::other(99);
  EXPECT_FALSE(status.success());
  EXPECT_FALSE(status.code().has_value());
  EXPECT_EQ(status.native(), 99u);
}

}  // namespace procly
