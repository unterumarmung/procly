load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "runfiles_support",
    hdrs = ["runfiles_support.hpp"],
    visibility = ["//visibility:public"],
    deps = ["@bazel_tools//tools/cpp/runfiles"],
)

cc_binary(
    name = "procly_child",
    srcs = ["procly_child.cc"],
    visibility = ["//visibility:public"],
    deps = ["//:procly"],
)
