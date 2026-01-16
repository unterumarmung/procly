#include <iostream>

#include "procly/command.hpp"

int main() {
  // clang-format off
  const auto cmd = procly::Command{"/bin/sh"}
                       .arg("-c")
                       .arg("printf 'out'; printf 'err' 1>&2")
                       .stdout(procly::Stdio::null())
                       .stderr(procly::Stdio::null());
  // clang-format on

  auto status = cmd.status();
  if (!status) {
    std::cerr << "status failed: " << status.error().context << " " << status.error().code.message()
              << "\n";
    return 1;
  }
  if (!status->success()) {
    std::cerr << "child failed\n";
    return 1;
  }

  return 0;
}
