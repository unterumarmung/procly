#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "procly/command.hpp"

namespace fs = std::filesystem;

namespace {
fs::path unique_path(const char* stem) {
  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  std::string name = "procly_example_";
  name.append(stem);
  name.push_back('_');
  name.append(std::to_string(static_cast<long long>(now)));
  return fs::temp_directory_path() / name;
}
}  // namespace

int main() {
  fs::path input_path = unique_path("in");
  fs::path output_path = unique_path("out");

  std::error_code remove_ec;
  fs::remove(input_path, remove_ec);
  fs::remove(output_path, remove_ec);

  std::string payload = "file-data";
  {
    std::ofstream input_file(input_path, std::ios::binary);
    if (!input_file) {
      std::cerr << "failed to open input file\n";
      return 1;
    }
    input_file << payload;
  }

  // clang-format off
  const auto cmd = procly::Command{"/bin/cat"}
                       .stdin(procly::Stdio::file(input_path))
                       .stdout(procly::Stdio::file(output_path));
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

  std::ifstream output_file(output_path, std::ios::binary);
  if (!output_file) {
    std::cerr << "failed to open output file\n";
    return 1;
  }
  std::string output((std::istreambuf_iterator<char>(output_file)),
                     std::istreambuf_iterator<char>());

  if (output != payload) {
    std::cerr << "unexpected output: " << output << "\n";
    return 1;
  }

  fs::remove(input_path, remove_ec);
  fs::remove(output_path, remove_ec);

  return 0;
}
