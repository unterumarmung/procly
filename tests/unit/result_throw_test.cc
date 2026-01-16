#include <gtest/gtest.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

#include "procly/result.hpp"

namespace procly {

TEST(ResultThrowTest, SystemCategoryThrowsSystemError) {
  Error error{std::error_code(ENOENT, std::system_category()), "open"};
  EXPECT_THROW(internal::throw_error(error), std::system_error);
}

TEST(ResultThrowTest, ProclyCategoryThrowsRuntimeError) {
  Error error{make_error_code(errc::timeout), "timeout"};
  EXPECT_THROW(internal::throw_error(error), std::runtime_error);
}

}  // namespace procly
