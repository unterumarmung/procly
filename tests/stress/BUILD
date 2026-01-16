load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "command_stress_test",
    srcs = ["command_stress_test.cc"],
    data = ["//tests/helpers:procly_child"],
    tags = [
        "manual",
        "stress",
    ],
    deps = [
        "//:procly",
        "//tests/helpers:runfiles_support",
        "@googletest//:gtest_main",
    ],
)

test_suite(
    name = "all",
    tags = ["stress"],
    tests = [
        ":command_stress_test",
    ],
)
