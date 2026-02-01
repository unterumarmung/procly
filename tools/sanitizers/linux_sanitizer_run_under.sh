#!/usr/bin/env bash
# Wrapper for Bazel --run_under on Linux to make UBSan shared runtimes available
# at test execution time without relying on non-sandboxed paths.
# This is test-only plumbing; production binaries are unaffected.
set -euo pipefail

# Bazel calls: <run_under> <test_binary> <args...>
BIN="${1:-}"
if [[ -z "$BIN" ]]; then
  echo "usage: $0 <binary> [args...]" >&2
  exit 2
fi
shift || true

BIN_ABS="$BIN"
if [[ "$BIN_ABS" != /* ]]; then
  BIN_ABS="$(pwd)/$BIN"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/sanitizer_runfiles_resolve.sh"

if ! /usr/bin/readelf -d "$BIN" 2>/dev/null | grep -q 'libclang_rt\.ubsan_standalone\.so'; then
  exec "$BIN" "$@"
fi

llvm_lib_dir="$(resolve_runfile "llvm_toolchain_llvm/lib/clang/20/lib" || true)"
if [[ -z "$llvm_lib_dir" ]]; then
  llvm_lib_dir="$(resolve_runfile "external/llvm_toolchain_llvm/lib/clang/20/lib" || true)"
fi
if [[ -z "$llvm_lib_dir" ]]; then
  echo "failed to locate llvm toolchain runtime in runfiles" >&2
  exit 1
fi

ubsan_so="$(/usr/bin/find "$llvm_lib_dir" -name 'libclang_rt.ubsan_standalone.so' -print -quit)"
if [[ -z "$ubsan_so" ]]; then
  echo "failed to locate libclang_rt.ubsan_standalone.so under $llvm_lib_dir" >&2
  exit 1
fi

ubsan_dir="$(dirname "$ubsan_so")"
export LD_LIBRARY_PATH="${ubsan_dir}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$BIN" "$@"
