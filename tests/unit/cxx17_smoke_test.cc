#include <gtest/gtest.h>

#include "procly/command.hpp"
#include "procly/status.hpp"

namespace procly {

TEST(Cxx17SmokeTest, CompilesAndLinksProclyCxx17) {
  Command cmd("echo");
  cmd.arg("hello");

  ExitStatus status = ExitStatus::exited(0);
  EXPECT_TRUE(status.success());
}

}  // namespace procly
