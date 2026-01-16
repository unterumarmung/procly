#include "procly/result.hpp"

#include <stdexcept>

namespace procly {

namespace {

class procly_error_category : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "procly"; }

  [[nodiscard]] std::string message(int value) const override {
    switch (static_cast<errc>(value)) {
      case errc::ok:
        return "ok";
      case errc::empty_argv:
        return "empty argv";
      case errc::invalid_stdio:
        return "invalid stdio";
      case errc::invalid_pipeline:
        return "invalid pipeline";
      case errc::pipe_failed:
        return "pipe failed";
      case errc::spawn_failed:
        return "spawn failed";
      case errc::wait_failed:
        return "wait failed";
      case errc::read_failed:
        return "read failed";
      case errc::write_failed:
        return "write failed";
      case errc::open_failed:
        return "open failed";
      case errc::close_failed:
        return "close failed";
      case errc::dup_failed:
        return "dup failed";
      case errc::chdir_failed:
        return "chdir failed";
      case errc::kill_failed:
        return "kill failed";
      case errc::timeout:
        return "timeout";
    }
    return "unknown error";
  }
};

}  // namespace

const std::error_category& error_category() noexcept {
  static procly_error_category category;
  return category;
}

std::error_code make_error_code(errc value) noexcept {
  return {static_cast<int>(value), error_category()};
}

namespace internal {

[[noreturn]] void throw_error(const Error& error) {
  if (error.code.category() == std::system_category()) {
    throw std::system_error(error.code, error.context);
  }
  throw std::runtime_error(error.context.empty() ? error.code.message() : error.context);
}

}  // namespace internal

}  // namespace procly
