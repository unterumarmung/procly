#include <iostream>

#include "procly/command.hpp"

int main() {
  // clang-format off
  const auto cmd = procly::Command{"/bin/sh"}
                       .arg("-c")
                       .arg("exit 7");
  // clang-format on

  auto status = cmd.status();
  if (!status) {
    std::cerr << "status failed: " << status.error().context << " " << status.error().code.message()
              << "\n";
    return 1;
  }

  const auto& st = status.value();
  if (!st.code().has_value() || st.code().value() != 7) {
    std::cerr << "unexpected exit code\n";
    return 1;
  }

  return 0;
}
