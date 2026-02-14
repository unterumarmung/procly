"""Targets in the repository root"""

load("@gazelle//:def.bzl", "gazelle")
load("@rules_cc//cc:defs.bzl", "cc_library")

exports_files(
    [
        ".clang-tidy",
        "README.md",
    ],
    visibility = ["//:__subpackages__"],
)

# We prefer BUILD instead of BUILD.bazel
# gazelle:build_file_name BUILD
# gazelle:exclude githooks/*

gazelle(
    name = "gazelle",
    env = {
        "ENABLE_LANGUAGES": ",".join([
            "starlark",
            "cc",
        ]),
    },
    gazelle = "@multitool//tools/gazelle",
)

PROCLY_SRCS = [
    "src/child.cc",
    "src/command.cc",
    "src/internal/clock.cc",
    "src/internal/io_drain.cc",
    "src/internal/lowering.cc",
    "src/internal/posix_backend.cc",
    "src/internal/posix_spawn.cc",
    "src/internal/wait_policy.cc",
    "src/pipe.cc",
    "src/pipeline.cc",
    "src/result.cc",
    "src/status.cc",
    "src/unix.cc",
]

PROCLY_HDRS = [
    "include/procly/child.hpp",
    "include/procly/command.hpp",
    "include/procly/internal/access.hpp",
    "include/procly/internal/backend.hpp",
    "include/procly/internal/clock.hpp",
    "include/procly/internal/expected.hpp",
    "include/procly/internal/fd.hpp",
    "include/procly/internal/io_drain.hpp",
    "include/procly/internal/lowering.hpp",
    "include/procly/internal/posix_spawn.hpp",
    "include/procly/internal/wait_policy.hpp",
    "include/procly/pipe.hpp",
    "include/procly/pipeline.hpp",
    "include/procly/platform.hpp",
    "include/procly/result.hpp",
    "include/procly/status.hpp",
    "include/procly/stdio.hpp",
    "include/procly/unix.hpp",
    "include/procly/windows.hpp",
]

PROCLY_INCLUDES = ["include"]

PROCLY_PUBLIC_VISIBILITY = ["//visibility:public"]

cc_library(
    name = "procly",
    srcs = PROCLY_SRCS,
    hdrs = PROCLY_HDRS,
    includes = PROCLY_INCLUDES,
    visibility = PROCLY_PUBLIC_VISIBILITY,
)

cc_library(
    name = "procly_force_fork",
    srcs = PROCLY_SRCS,
    hdrs = PROCLY_HDRS,
    defines = ["PROCLY_FORCE_FORK"],
    includes = PROCLY_INCLUDES,
    visibility = PROCLY_PUBLIC_VISIBILITY,
)
