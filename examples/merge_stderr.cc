#include <iostream>
#include <string>

#include "procly/command.hpp"

int main() {
  procly::SpawnOptions opts;
  opts.merge_stderr_into_stdout = true;
  // clang-format off
  const auto cmd = procly::Command{"/bin/sh"}
                       .arg("-c")
                       .arg("printf 'out'; printf 'err' 1>&2")
                       .options(opts);
  // clang-format on

  auto out = cmd.output();
  if (!out) {
    std::cerr << "output failed: " << out.error().context << " " << out.error().code.message()
              << "\n";
    return 1;
  }

  const auto& output = out.value();
  if (!output.stderr_data.empty() || output.stdout_data != "outerr") {
    std::cerr << "unexpected merged output: stdout='" << output.stdout_data << "' stderr='"
              << output.stderr_data << "'\n";
    return 1;
  }

  return 0;
}
