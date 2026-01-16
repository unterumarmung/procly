#include <filesystem>
#include <iostream>
#include <string>

#include "procly/command.hpp"

namespace fs = std::filesystem;

int main() {
  fs::path cwd = fs::temp_directory_path();

  // clang-format off
  const auto cmd = procly::Command{"/bin/pwd"}
                       .current_dir(cwd);
  // clang-format on

  auto out = cmd.output();
  if (!out) {
    std::cerr << "cwd output failed: " << out.error().context << " " << out.error().code.message()
              << "\n";
    return 1;
  }

  std::string reported = out->stdout_data;
  while (!reported.empty() && (reported.back() == '\n' || reported.back() == '\r')) {
    reported.pop_back();
  }

  std::error_code ec;
  if (!fs::equivalent(reported, cwd, ec)) {
    std::cerr << "unexpected cwd output: " << reported;
    if (ec) {
      std::cerr << " (" << ec.message() << ")";
    }
    std::cerr << "\n";
    return 1;
  }

  return 0;
}
