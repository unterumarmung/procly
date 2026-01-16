#include <iostream>

#include "procly/command.hpp"
#include "procly/pipeline.hpp"

int main() {
  // clang-format off
  const auto bad = procly::Command{"/bin/sh"}
                       .arg("-c")
                       .arg("exit 7");
  const auto good = procly::Command{"/bin/cat"};

  const auto pipeline = (bad | good)
                            .pipefail(true);
  // clang-format on

  auto status = pipeline.status();
  if (!status) {
    std::cerr << "pipeline status failed: " << status.error().context << " "
              << status.error().code.message() << "\n";
    return 1;
  }

  const auto& st = status.value();
  if (!st.code().has_value() || st.code().value() != 7) {
    std::cerr << "unexpected pipefail status\n";
    return 1;
  }

  return 0;
}
