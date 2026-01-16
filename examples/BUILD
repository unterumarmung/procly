load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "command_output",
    srcs = ["command_output.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "command_status",
    srcs = ["command_status.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "command_env",
    srcs = ["command_env.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "command_cwd",
    srcs = ["command_cwd.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "stdio_pipes",
    srcs = ["stdio_pipes.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "stdio_files",
    srcs = ["stdio_files.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "stdio_null",
    srcs = ["stdio_null.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "stdio_append",
    srcs = ["stdio_append.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "merge_stderr",
    srcs = ["merge_stderr.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "wait_timeout",
    srcs = ["wait_timeout.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "try_wait",
    srcs = ["try_wait.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "pipeline_output",
    srcs = ["pipeline_output.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "pipeline_pipefail",
    srcs = ["pipeline_pipefail.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

cc_test(
    name = "pipeline_pipes",
    srcs = ["pipeline_pipes.cc"],
    tags = ["examples"],
    deps = ["//:procly"],
)

test_suite(
    name = "all",
    tags = ["examples"],
    tests = [
        ":command_cwd",
        ":command_env",
        ":command_output",
        ":command_status",
        ":merge_stderr",
        ":pipeline_output",
        ":pipeline_pipefail",
        ":pipeline_pipes",
        ":stdio_append",
        ":stdio_files",
        ":stdio_null",
        ":stdio_pipes",
        ":try_wait",
        ":wait_timeout",
    ],
)
