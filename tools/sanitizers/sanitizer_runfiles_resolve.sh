#!/usr/bin/env bash
# Shared runfiles resolver for sanitizer run_under wrappers.
set -euo pipefail

resolve_runfile() {
  local path="$1"
  local runfiles_dir="${RUNFILES_DIR:-}"
  local test_srcdir="${TEST_SRCDIR:-}"
  local manifest="${RUNFILES_MANIFEST_FILE:-}"
  local workspace="${TEST_WORKSPACE:-}"

  if [[ -z "$runfiles_dir" && -n "${BIN_ABS:-}" && -d "${BIN_ABS}.runfiles" ]]; then
    runfiles_dir="${BIN_ABS}.runfiles"
  fi
  if [[ -z "$test_srcdir" && -n "${BIN_ABS:-}" && -d "${BIN_ABS}.runfiles" ]]; then
    test_srcdir="${BIN_ABS}.runfiles"
  fi
  if [[ -z "$manifest" && -n "${BIN_ABS:-}" && -f "${BIN_ABS}.runfiles_manifest" ]]; then
    manifest="${BIN_ABS}.runfiles_manifest"
  fi

  if [[ -n "$runfiles_dir" && -e "${runfiles_dir}/${path}" ]]; then
    echo "${runfiles_dir}/${path}"
    return 0
  fi
  if [[ -n "$runfiles_dir" && -n "$workspace" && -e "${runfiles_dir}/${workspace}/${path}" ]]; then
    echo "${runfiles_dir}/${workspace}/${path}"
    return 0
  fi
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
  if [[ -n "$test_srcdir" && -e "${test_srcdir}/${path}" ]]; then
    echo "${test_srcdir}/${path}"
    return 0
  fi
  if [[ -n "$test_srcdir" && -n "$workspace" && -e "${test_srcdir}/${workspace}/${path}" ]]; then
    echo "${test_srcdir}/${workspace}/${path}"
    return 0
  fi
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
  fi
  return 1
}
