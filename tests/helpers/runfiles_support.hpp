#pragma once

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "tools/cpp/runfiles/runfiles.h"

namespace procly::support {

namespace fs = std::filesystem;

inline std::string helper_path(const char* argv0) {
  const char* override_path = std::getenv("PROCLY_HELPER_PATH");
  if (override_path && fs::exists(override_path)) {
    return override_path;
  }

  std::string error;
  auto runfiles = bazel::tools::cpp::runfiles::Runfiles::Create(argv0, &error);
  if (!runfiles) {
    std::cerr << "runfiles init failed: " << error << "\n";
    return "";
  }

  std::string path = runfiles->Rlocation("procly/tests/helpers/procly_child");
  if (path.empty()) {
    path = runfiles->Rlocation("tests/helpers/procly_child");
  }
  if (!path.empty() && fs::exists(path)) {
    return path;
  }

  const char* srcdir = std::getenv("TEST_SRCDIR");
  const char* workspace = std::getenv("TEST_WORKSPACE");
  if (srcdir && workspace) {
    fs::path fallback = fs::path(srcdir) / workspace / "tests/helpers/procly_child";
    if (fs::exists(fallback)) {
      return fallback.string();
    }
  }

  fs::path runfiles_dir = fs::path(argv0).string() + ".runfiles";
  if (fs::exists(runfiles_dir)) {
    for (const auto& entry : fs::recursive_directory_iterator(runfiles_dir)) {
      if (entry.is_regular_file() && entry.path().filename() == "procly_child") {
        return entry.path().string();
      }
    }
  }

  std::cerr << "helper path not found\n";
  return "";
}

}  // namespace procly::support
