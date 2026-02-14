load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "command_integration_test",
    srcs = ["command_integration_test.cc"],
    data = ["//tests/helpers:procly_child"],
    tags = ["integration"],
    deps = [
        "//:procly",
        "//tests/helpers:runfiles_support",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "command_integration_test_force_fork",
    srcs = ["command_integration_test.cc"],
    copts = ["-DPROCLY_FORCE_FORK"],
    data = ["//tests/helpers:procly_child"],
    linkopts = select({
        "@platforms//os:linux": ["-ldl"],
        "//conditions:default": [],
    }),
    tags = ["integration"],
    deps = [
        "//:procly_force_fork",
        "//tests/helpers:runfiles_support",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "command_integration_test_force_spawn",
    srcs = ["command_integration_test.cc"],
    copts = ["-DPROCLY_FORCE_POSIX_SPAWN"],
    data = ["//tests/helpers:procly_child"],
    tags = ["integration"],
    deps = [
        "//:procly",
        "//tests/helpers:runfiles_support",
        "@googletest//:gtest_main",
    ],
)

test_suite(
    name = "all",
    tags = ["integration"],
    tests = [
        ":command_integration_test",
        ":command_integration_test_force_fork",
        ":command_integration_test_force_spawn",
    ],
)
