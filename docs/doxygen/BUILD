load(":vars.bzl", "doxygen_vars")

package(default_visibility = ["//visibility:public"])

doxygen_vars(
    name = "doc_vars",
    locations = {
        "AWESOME_CSS": "@doxygen_awesome_css//:awesome_css",
        "AWESOME_SIDEBAR_CSS": "@doxygen_awesome_css//:awesome_sidebar_css",
        "AWESOME_DARKMODE_TOGGLE_CSS": "@doxygen_awesome_css//:awesome_darkmode_toggle_css",
        "AWESOME_DARKMODE_TOGGLE_JS": "@doxygen_awesome_css//:awesome_darkmode_toggle_js",
        "AWESOME_COPY_JS": "@doxygen_awesome_css//:awesome_copy_js",
        "AWESOME_PARALINK_JS": "@doxygen_awesome_css//:awesome_paralink_js",
        "CUSTOM_CSS": "//docs/doxygen:custom.css",
        "HTML_HEADER": "//docs/doxygen:header.html",
    },
)
