load("@doxygen//:doxygen.bzl", "doxygen")

package(default_visibility = ["//visibility:public"])

doxygen(
    name = "api_docs",
    srcs = [
        "//:README.md",
        "//docs/doxygen:custom.css",
        "//docs/doxygen:header.html",
        "@doxygen_awesome_css//:awesome_copy_js",
        "@doxygen_awesome_css//:awesome_css",
        "@doxygen_awesome_css//:awesome_darkmode_toggle_css",
        "@doxygen_awesome_css//:awesome_darkmode_toggle_js",
        "@doxygen_awesome_css//:awesome_paralink_js",
        "@doxygen_awesome_css//:awesome_sidebar_css",
    ],
    configurations = [
        "HTML_EXTRA_STYLESHEET = $(AWESOME_CSS) $(AWESOME_SIDEBAR_CSS) $(AWESOME_DARKMODE_TOGGLE_CSS) $(CUSTOM_CSS)",
        "HTML_EXTRA_FILES = $(AWESOME_DARKMODE_TOGGLE_JS) $(AWESOME_COPY_JS) $(AWESOME_PARALINK_JS)",
        "HTML_HEADER = $(HTML_HEADER)",
        "HTML_COPY_CLIPBOARD = NO",
        "HTML_COLORSTYLE = LIGHT",
        "GENERATE_TREEVIEW = YES",
        "FULL_SIDEBAR = NO",
        "EXCLUDE_PATTERNS = */include/procly/internal/* */src/*",
        "MARKDOWN_ID_STYLE = GITHUB",
        "WARN_AS_ERROR = YES",
        "QUIET = YES",
    ],
    generate_html = True,
    generate_latex = False,
    project_brief = "Cross-platform process library for modern C++.",
    project_name = "procly",
    tags = ["manual"],
    toolchains = ["//docs/doxygen:doc_vars"],
    use_mdfile_as_mainpage = "README.md",
    deps = ["//:procly"],
)
