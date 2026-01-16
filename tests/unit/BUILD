load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "result_test",
    srcs = ["result_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "result_throw_test",
    srcs = ["result_throw_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "status_test",
    srcs = ["status_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "stdio_test",
    srcs = ["stdio_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "pipe_test",
    srcs = ["pipe_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "lowering_test",
    srcs = ["lowering_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "backend_injection_test",
    srcs = ["backend_injection_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "posix_spawn_test",
    srcs = ["posix_spawn_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "io_drain_test",
    srcs = ["io_drain_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "unix_test",
    srcs = ["unix_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "timeout_policy_test",
    srcs = ["timeout_policy_test.cc"],
    deps = [
        "//:procly",
        "@googletest//:gtest_main",
    ],
)

test_suite(
    name = "all",
    tests = [
        ":backend_injection_test",
        ":io_drain_test",
        ":lowering_test",
        ":pipe_test",
        ":posix_spawn_test",
        ":result_test",
        ":result_throw_test",
        ":status_test",
        ":stdio_test",
        ":timeout_policy_test",
        ":unix_test",
    ],
)
