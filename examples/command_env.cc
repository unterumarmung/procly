#include <iostream>
#include <string>

#include "procly/command.hpp"

int main() {
  // clang-format off
  const auto cmd = procly::Command{"/usr/bin/env"}
                       .env_clear()
                       .env("PROCLY_EXAMPLE_KEEP", "keep")
                       .env("PROCLY_EXAMPLE_DROP", "drop")
                       .env_remove("PROCLY_EXAMPLE_DROP");
  // clang-format on

  auto out = cmd.output();
  if (!out) {
    std::cerr << "env output failed: " << out.error().context << " " << out.error().code.message()
              << "\n";
    return 1;
  }

  const std::string& data = out->stdout_data;
  if (data.find("PROCLY_EXAMPLE_KEEP=keep") == std::string::npos) {
    std::cerr << "missing env var in output\n";
    return 1;
  }
  if (data.find("PROCLY_EXAMPLE_DROP=") != std::string::npos) {
    std::cerr << "env_remove did not drop variable\n";
    return 1;
  }

  return 0;
}
