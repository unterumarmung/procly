#include <iostream>

#include "procly/command.hpp"
#include "procly/pipeline.hpp"

int main() {
  // clang-format off
  const auto first = procly::Command{"/bin/sh"}
                         .arg("-c")
                         .arg("exit 3");
  const auto second = procly::Command{"/bin/sh"}
                          .arg("-c")
                          .arg("exit 1");
  const auto third = procly::Command{"/bin/cat"};

  const auto pipeline = (first | second | third)
                            .pipefail(true);
  // clang-format on

  auto status = pipeline.status();
  if (!status) {
    std::cerr << "pipeline status failed: " << status.error().context << " "
              << status.error().code.message() << "\n";
    return 1;
  }

  const auto& st = status.value();
  if (!st.code().has_value() || st.code().value() != 1) {
    std::cerr << "unexpected pipefail status: " << st.code().value_or(-1) << "\n";
    return 1;
  }

  return 0;
}
