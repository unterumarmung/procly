#include <chrono>
#include <iostream>

#include "procly/command.hpp"
#include "procly/result.hpp"

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

  procly::WaitOptions options;
  options.timeout = std::chrono::milliseconds(10);
  options.kill_grace = std::chrono::milliseconds(10);
  auto wait_result = child_result->wait(options);
  if (wait_result) {
    std::cerr << "expected timeout but process exited\n";
    return 1;
  }
  if (wait_result.error().code != procly::make_error_code(procly::errc::timeout)) {
    std::cerr << "unexpected wait error: " << wait_result.error().code.message() << "\n";
    return 1;
  }

  return 0;
}
