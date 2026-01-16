#include <iostream>
#include <string>

#include "procly/command.hpp"
#include "procly/pipeline.hpp"

int main() {
  // clang-format off
  const auto producer = procly::Command{"/bin/echo"}
                            .arg("ping");
  const auto consumer = procly::Command{"/usr/bin/tr"}
                            .arg("a-z")
                            .arg("A-Z");
  // clang-format on

  procly::Pipeline pipeline = producer | consumer;
  auto output = pipeline.output();
  if (!output) {
    std::cerr << "pipeline output failed: " << output.error().context << " "
              << output.error().code.message() << "\n";
    return 1;
  }

  const auto& data = output.value();
  if (data.stdout_data != "PING\n") {
    std::cerr << "unexpected pipeline output: " << data.stdout_data << "\n";
    return 1;
  }

  return 0;
}
