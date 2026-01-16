#include "procly/result.hpp"

#include <gtest/gtest.h>

namespace procly {

struct ArrowProbe {
  int value = 0;
  int get() const { return value; }
};

TEST(ResultTest, ErrorCategoryMessageNotEmpty) {
  auto code = make_error_code(errc::timeout);
  EXPECT_FALSE(code.message().empty());
}

TEST(ResultTest, ErrorCodeEquality) {
  std::error_code code = errc::timeout;
  EXPECT_EQ(code, make_error_code(errc::timeout));
}

TEST(ResultTest, ExpectedValueAndError) {
  Result<int> ok(5);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value(), 5);

  Error err{make_error_code(errc::spawn_failed), "spawn"};
  Result<int> bad(err);
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error().code, err.code);
  EXPECT_EQ(bad.error().context, "spawn");
}

TEST(ResultTest, ExpectedOperatorArrowProvidesMemberAccess) {
  Result<ArrowProbe> ok(ArrowProbe{42});
  EXPECT_EQ(ok->value, 42);
  EXPECT_EQ(ok->get(), 42);

  const Result<ArrowProbe> const_ok(ArrowProbe{7});
  EXPECT_EQ(const_ok->get(), 7);
}

}  // namespace procly
