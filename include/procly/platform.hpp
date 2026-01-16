#pragma once

/// @file platform.hpp
/// @brief Platform and standard feature detection macros.

#include <version>

// Centralized platform detection macros.
// Values are 0 or 1 for use in #if expressions.

#if defined(_WIN32) || defined(_WIN64)
/// @brief True when building for Windows.
#define PROCLY_PLATFORM_WINDOWS 1
#else
/// @brief True when building for Windows.
#define PROCLY_PLATFORM_WINDOWS 0
#endif

#if defined(__APPLE__) && defined(__MACH__)
/// @brief True when building for macOS.
#define PROCLY_PLATFORM_MACOS 1
#else
/// @brief True when building for macOS.
#define PROCLY_PLATFORM_MACOS 0
#endif

#if defined(__linux__)
/// @brief True when building for Linux.
#define PROCLY_PLATFORM_LINUX 1
#else
/// @brief True when building for Linux.
#define PROCLY_PLATFORM_LINUX 0
#endif

#if !PROCLY_PLATFORM_WINDOWS && \
    (defined(__unix__) || PROCLY_PLATFORM_MACOS || PROCLY_PLATFORM_LINUX)
// NOLINTNEXTLINE(modernize-macro-to-enum)
/// @brief True when building for a POSIX-like platform.
#define PROCLY_PLATFORM_POSIX 1
#else
// NOLINTNEXTLINE(modernize-macro-to-enum)
/// @brief True when building for a POSIX-like platform.
#define PROCLY_PLATFORM_POSIX 0
#endif

// C++ standard feature detection used across procly.
#if defined(_MSVC_LANG) && _MSVC_LANG > __cplusplus
/// @brief Active C++ language version (MSVC uses _MSVC_LANG).
#define PROCLY_CPLUSPLUS _MSVC_LANG
#else
/// @brief Active C++ language version.
#define PROCLY_CPLUSPLUS __cplusplus
#endif

#if PROCLY_CPLUSPLUS < 201703L
#error "procly requires at least C++17"
#endif

#if PROCLY_CPLUSPLUS >= 202002L
// NOLINTNEXTLINE(modernize-macro-to-enum)
/// @brief True when compiling with C++20 or later.
#define PROCLY_HAS_CXX20 1
#else
// NOLINTNEXTLINE(modernize-macro-to-enum)
/// @brief True when compiling with C++20 or later.
#define PROCLY_HAS_CXX20 0
#endif

#if defined(__cpp_lib_span) && (__cpp_lib_span >= 202002L)
// NOLINTNEXTLINE(modernize-macro-to-enum)
/// @brief True when std::span is available.
#define PROCLY_HAS_STD_SPAN 1
#else
// NOLINTNEXTLINE(modernize-macro-to-enum)
/// @brief True when std::span is available.
#define PROCLY_HAS_STD_SPAN 0
#endif
