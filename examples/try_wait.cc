#include <iostream>

#include "procly/command.hpp"

int main() {
  // clang-format off
  const auto cmd = procly::Command{"/bin/sleep"}
                       .arg("1");
  // clang-format on

  auto child_result = cmd.spawn();
  if (!child_result) {
    std::cerr << "spawn failed: " << child_result.error().context << " "
              << child_result.error().code.message() << "\n";
    return 1;
  }

  auto try_result = child_result->try_wait();
  if (!try_result) {
    std::cerr << "try_wait failed: " << try_result.error().context << " "
              << try_result.error().code.message() << "\n";
    return 1;
  }

  if (try_result->has_value()) {
    if (!try_result->value().success()) {
      std::cerr << "unexpected non-success status\n";
      return 1;
    }
    return 0;
  }

  auto wait_result = child_result->wait();
  if (!wait_result) {
    std::cerr << "wait failed: " << wait_result.error().context << " "
              << wait_result.error().code.message() << "\n";
    return 1;
  }
  if (!wait_result->success()) {
    std::cerr << "child failed\n";
    return 1;
  }

  return 0;
}
