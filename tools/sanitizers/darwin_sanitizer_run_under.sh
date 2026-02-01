#!/usr/bin/env bash
# Wrapper for Bazel --run_under on macOS to make sanitizer runtimes available at
# test execution time without hardcoding Bazel's external/ layout or clang version.
#
# High level flow:
# 1) Check whether the test binary references libclang_rt.*_osx_dynamic.dylib.
# 2) If not, just exec the test directly (no overhead).
# 3) If yes, locate the toolchain's clang distribution via runfiles.
# 4) Copy the needed runtime dylibs next to a temp copy of the test binary.
# 5) Add @loader_path as an rpath so the temp binary can find the dylibs.
# 6) Also stage the helper binary (procly_child) used by integration tests.
set -euo pipefail

# Bazel calls: <run_under> <test_binary> <args...>
BIN="${1:-}"
if [[ -z "$BIN" ]]; then
  echo "usage: $0 <binary> [args...]" >&2
  exit 2
fi
shift || true
# Normalize to an absolute path for runfiles discovery fallbacks.
BIN_ABS="$BIN"
if [[ "$BIN_ABS" != /* ]]; then
  BIN_ABS="$(pwd)/$BIN"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/sanitizer_runfiles_resolve.sh"

# Inspect the binary's DT_NEEDED entries and pick only sanitizer dylibs.
NEEDED="$(
  /usr/bin/otool -L "$BIN" \
    | awk '{print $1}' \
    | grep -E 'libclang_rt\..*_osx_dynamic\.dylib$' \
    || true
)"
# Fast path: no sanitizer runtime referenced -> no wrapper needed.
if [[ -z "$NEEDED" ]]; then
  exec "$BIN" "$@"
fi

# Locate clang inside the toolchain distribution runfiles.
CLANG="$(resolve_runfile "llvm_toolchain_llvm/bin/clang" || true)"
if [[ -z "$CLANG" ]]; then
  echo "Could not resolve runfile: llvm_toolchain_llvm/bin/clang" >&2
  exit 1
fi
# The runtime dylibs live under <prefix>/lib/clang/<ver>/lib/darwin.
LLVM_PREFIX="$(cd "$(dirname "$CLANG")/.." && pwd)"
RUNTIME_DIR="$(ls -d "$LLVM_PREFIX"/lib/clang/*/lib/darwin 2>/dev/null | head -n 1 || true)"
if [[ -z "$RUNTIME_DIR" ]]; then
  echo "Could not locate runtime dir under: $LLVM_PREFIX/lib/clang/*/lib/darwin" >&2
  exit 1
fi

# Stage binaries + sanitizer runtimes in an isolated temp directory.
TMPDIR="$(mktemp -d "${TMPDIR:-/tmp}/bazel_san.XXXXXX")"
trap 'rm -rf "$TMPDIR"' EXIT

# Copy the test binary; we will adjust rpath on the copy.
BIN_BASE="$(basename "$BIN")"
cp -f "$BIN" "$TMPDIR/$BIN_BASE"
chmod +w "$TMPDIR/$BIN_BASE" || true

# Copy the exact sanitizer dylibs referenced by a binary into TMPDIR.
copy_needed_libs() {
  local list="$1"
  local dep=""
  for dep in $list; do
    local lib=""
    lib="$(basename "$dep")"
    if [[ -f "$RUNTIME_DIR/$lib" ]]; then
      cp -f "$RUNTIME_DIR/$lib" "$TMPDIR/$lib"
    else
      echo "Missing sanitizer runtime: $RUNTIME_DIR/$lib" >&2
      exit 1
    fi
  done
}

# Stage sanitizer runtimes for the main test binary.
copy_needed_libs "$NEEDED"

# Some tests spawn a helper binary. Stage it too and point tests at it.
HELPER=""
for candidate in "tests/helpers/procly_child" "procly/tests/helpers/procly_child"; do
  HELPER="$(resolve_runfile "$candidate" || true)"
  if [[ -n "$HELPER" && -f "$HELPER" ]]; then
    break
  fi
done

if [[ -n "$HELPER" && -f "$HELPER" ]]; then
  # Copy helper and its sanitizer deps next to the main test binary.
  HELPER_BASE="$(basename "$HELPER")"
  cp -f "$HELPER" "$TMPDIR/$HELPER_BASE"
  chmod +w "$TMPDIR/$HELPER_BASE" || true
  HELPER_NEEDED="$(
    /usr/bin/otool -L "$HELPER" \
      | awk '{print $1}' \
      | grep -E 'libclang_rt\..*_osx_dynamic\.dylib$' \
      || true
  )"
  copy_needed_libs "$HELPER_NEEDED"
  /usr/bin/install_name_tool -add_rpath "@loader_path" "$TMPDIR/$HELPER_BASE" 2>/dev/null || true
  # Tests consult this env var to find the helper; see runfiles_support.hpp.
  export PROCLY_HELPER_PATH="$TMPDIR/$HELPER_BASE"
fi

# Add @loader_path so the staged binary can find dylibs in TMPDIR.
/usr/bin/install_name_tool -add_rpath "@loader_path" "$TMPDIR/$BIN_BASE" 2>/dev/null || true

# Preserve Bazel's test environment variables and execute the staged binary.
exec "$TMPDIR/$BIN_BASE" "$@"
