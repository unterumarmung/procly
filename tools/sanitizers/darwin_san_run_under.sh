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

# Resolve a runfile path in a variety of Bazel runtime layouts:
# - Directory runfiles via RUNFILES_DIR / TEST_SRCDIR.
# - Manifest runfiles via RUNFILES_MANIFEST_FILE.
# - Canonical repo names via _repo_mapping (bzlmod).
# - Workspace-prefixed entries (TEST_WORKSPACE).
resolve_runfile() {
  local path="$1"
  local runfiles_dir="${RUNFILES_DIR:-}"
  local test_srcdir="${TEST_SRCDIR:-}"
  local manifest="${RUNFILES_MANIFEST_FILE:-}"
  local workspace="${TEST_WORKSPACE:-}"
  # Fall back to <binary>.runfiles when Bazel doesn't set env vars.
  if [[ -z "$runfiles_dir" && -d "${BIN_ABS}.runfiles" ]]; then
    runfiles_dir="${BIN_ABS}.runfiles"
  fi
  if [[ -z "$test_srcdir" && -d "${BIN_ABS}.runfiles" ]]; then
    test_srcdir="${BIN_ABS}.runfiles"
  fi
  if [[ -z "$manifest" && -f "${BIN_ABS}.runfiles_manifest" ]]; then
    manifest="${BIN_ABS}.runfiles_manifest"
  fi
  # Direct runfiles lookup.
  if [[ -n "$runfiles_dir" && -e "${runfiles_dir}/${path}" ]]; then
    echo "${runfiles_dir}/${path}"
    return 0
  fi
  # Some runfiles trees include the workspace name as a prefix.
  if [[ -n "$runfiles_dir" && -n "$workspace" && -e "${runfiles_dir}/${workspace}/${path}" ]]; then
    echo "${runfiles_dir}/${workspace}/${path}"
    return 0
  fi
  # When bzlmod is used, map apparent repo names to canonical ones.
  if [[ -n "$runfiles_dir" && -f "${runfiles_dir}/_repo_mapping" ]]; then
    local repo="${path%%/*}"
    local rest="${path#*/}"
    if [[ "$repo" != "$path" ]]; then
      local mapped=""
      mapped="$(awk -F',' -v r="$repo" '$2 == r {print $3; exit 0}' "${runfiles_dir}/_repo_mapping")"
      if [[ -n "$mapped" && -e "${runfiles_dir}/${mapped}/${rest}" ]]; then
        echo "${runfiles_dir}/${mapped}/${rest}"
        return 0
      fi
    fi
  fi
  # TEST_SRCDIR is often the same as RUNFILES_DIR but not always.
  if [[ -n "$test_srcdir" && -e "${test_srcdir}/${path}" ]]; then
    echo "${test_srcdir}/${path}"
    return 0
  fi
  if [[ -n "$test_srcdir" && -n "$workspace" && -e "${test_srcdir}/${workspace}/${path}" ]]; then
    echo "${test_srcdir}/${workspace}/${path}"
    return 0
  fi
  # Manifest fallback for runfiles, including workspace-prefixed entries.
  if [[ -n "$manifest" && -f "$manifest" ]]; then
    local resolved=""
    resolved="$(awk -v p="$path" '$1 == p {print $2; exit 0}' "${manifest}")"
    if [[ -n "$resolved" ]]; then
      echo "$resolved"
      return 0
    fi
    if [[ -n "$workspace" ]]; then
      resolved="$(awk -v p="$workspace/$path" '$1 == p {print $2; exit 0}' "${manifest}")"
      if [[ -n "$resolved" ]]; then
        echo "$resolved"
        return 0
      fi
    fi
    resolved="$(awk -v p="$path" '$1 ~ (p "$") {print $2; exit 0}' "${manifest}")"
    if [[ -n "$resolved" ]]; then
      echo "$resolved"
      return 0
    fi
    if [[ -n "$workspace" ]]; then
      resolved="$(awk -v p="$workspace/$path" '$1 ~ (p "$") {print $2; exit 0}' "${manifest}")"
      if [[ -n "$resolved" ]]; then
        echo "$resolved"
        return 0
      fi
    fi
  fi
  return 1
}

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
