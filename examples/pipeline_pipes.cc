#include <iostream>
#include <string>

#include "procly/command.hpp"
#include "procly/pipeline.hpp"

int main() {
  // clang-format off
  const auto first = procly::Command{"/bin/cat"};
  const auto second = procly::Command{"/usr/bin/tr"}
                          .arg("a-z")
                          .arg("A-Z");

  const auto pipeline = (first | second)
                            .stdin(procly::Stdio::piped())
                            .stdout(procly::Stdio::piped());
  // clang-format on

  auto child_result = pipeline.spawn();
  if (!child_result) {
    std::cerr << "pipeline spawn failed: " << child_result.error().context << " "
              << child_result.error().code.message() << "\n";
    return 1;
  }

  auto stdin_pipe = child_result->take_stdin();
  auto stdout_pipe = child_result->take_stdout();
  if (!stdin_pipe.has_value() || !stdout_pipe.has_value()) {
    std::cerr << "missing stdin/stdout pipes\n";
    return 1;
  }

  std::string payload = "ping";
  auto write_result = stdin_pipe->write_all(payload);
  if (!write_result) {
    std::cerr << "write failed: " << write_result.error().context << " "
              << write_result.error().code.message() << "\n";
    return 1;
  }
  stdin_pipe->close();

  auto read_result = stdout_pipe->read_all();
  if (!read_result) {
    std::cerr << "read failed: " << read_result.error().context << " "
              << read_result.error().code.message() << "\n";
    return 1;
  }

  auto wait_result = child_result->wait();
  if (!wait_result) {
    std::cerr << "wait failed: " << wait_result.error().context << " "
              << wait_result.error().code.message() << "\n";
    return 1;
  }
  if (!wait_result->aggregate.success()) {
    std::cerr << "pipeline failed\n";
    return 1;
  }

  if (read_result.value() != "PING") {
    std::cerr << "unexpected output: " << read_result.value() << "\n";
    return 1;
  }

  return 0;
}
