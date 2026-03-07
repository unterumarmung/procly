#!/usr/bin/env bash
set -euo pipefail

TMPDIR="$(mktemp -d "${TMPDIR:-/tmp}/sanitizer_runfiles_resolve_test.XXXXXX")"
trap 'rm -rf "$TMPDIR"' EXIT

MANIFEST="$TMPDIR/test.runfiles_manifest"
REPO_MAPPING="$TMPDIR/test.repo_mapping"
EMPTY_REPO_MAPPING="$TMPDIR/empty.repo_mapping"
EMPTY_MANIFEST="$TMPDIR/empty.runfiles_manifest"
CLANG="$TMPDIR/clang"
HELPER="$TMPDIR/procly_child"

touch "$CLANG" "$HELPER"

cat >"$REPO_MAPPING" <<EOF
,llvm_toolchain_llvm,toolchains_llvm++llvm+llvm_toolchain_llvm
toolchains_llvm++llvm+llvm_toolchain,llvm_toolchain_llvm,toolchains_llvm++llvm+llvm_toolchain_llvm
toolchains_llvm++llvm+llvm_toolchain_llvm,llvm_toolchain_llvm,toolchains_llvm++llvm+llvm_toolchain_llvm
EOF

cat >"$MANIFEST" <<EOF
_repo_mapping $REPO_MAPPING
toolchains_llvm++llvm+llvm_toolchain_llvm/bin/clang $CLANG
_main/tests/helpers/procly_child $HELPER
EOF

: >"$EMPTY_REPO_MAPPING"

cat >"$EMPTY_MANIFEST" <<EOF
_repo_mapping $EMPTY_REPO_MAPPING
toolchains_llvm++llvm+llvm_toolchain_llvm/bin/clang $CLANG
EOF

unset RUNFILES_DIR TEST_SRCDIR
export RUNFILES_MANIFEST_FILE="$MANIFEST"
export TEST_WORKSPACE="_main"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/sanitizer_runfiles_resolve.sh"

resolved_clang="$(resolve_runfile "llvm_toolchain_llvm/bin/clang")"
if [[ "$resolved_clang" != "$CLANG" ]]; then
  echo "expected clang path '$CLANG', got '$resolved_clang'" >&2
  exit 1
fi

resolved_helper="$(resolve_runfile "tests/helpers/procly_child")"
if [[ "$resolved_helper" != "$HELPER" ]]; then
  echo "expected helper path '$HELPER', got '$resolved_helper'" >&2
  exit 1
fi

export RUNFILES_MANIFEST_FILE="$EMPTY_MANIFEST"

resolved_clang_without_repo_mapping="$(resolve_runfile "llvm_toolchain_llvm/bin/clang")"
if [[ "$resolved_clang_without_repo_mapping" != "$CLANG" ]]; then
  echo "expected clang path '$CLANG' with empty repo_mapping, got '$resolved_clang_without_repo_mapping'" >&2
  exit 1
fi

resolved_clang_by_suffix="$(resolve_runfile_suffix "bin/clang")"
if [[ "$resolved_clang_by_suffix" != "$CLANG" ]]; then
  echo "expected clang path '$CLANG' via suffix lookup, got '$resolved_clang_by_suffix'" >&2
  exit 1
fi
