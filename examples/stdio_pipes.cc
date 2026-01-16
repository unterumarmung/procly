#include <iostream>
#include <string>

#include "procly/command.hpp"
#include "procly/pipe.hpp"

int main() {
  // clang-format off
  const auto cmd = procly::Command{"/bin/cat"}
                       .stdin(procly::Stdio::piped())
                       .stdout(procly::Stdio::piped());
  // clang-format on

  auto child_result = cmd.spawn();
  if (!child_result) {
    std::cerr << "spawn failed: " << child_result.error().context << " "
              << child_result.error().code.message() << "\n";
    return 1;
  }

  procly::Child child = std::move(child_result.value());
  auto stdin_pipe = child.take_stdin();
  auto stdout_pipe = child.take_stdout();
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

  auto status = child.wait();
  if (!status) {
    std::cerr << "wait failed: " << status.error().context << " " << status.error().code.message()
              << "\n";
    return 1;
  }
  if (!status->success()) {
    std::cerr << "child failed\n";
    return 1;
  }

  if (read_result.value() != payload) {
    std::cerr << "unexpected output: " << read_result.value() << "\n";
    return 1;
  }

  return 0;
}
