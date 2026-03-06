#!/usr/bin/env bash
# Shared runfiles resolver for sanitizer run_under wrappers.
set -euo pipefail

manifest_lookup() {
  local manifest="$1"
  local key="$2"
  awk -v p="$key" '$1 == p { $1 = ""; sub(/^ /, ""); print; exit 0 }' "$manifest"
}

repo_map_lookup() {
  local repo_mapping="$1"
  local repo="$2"
  awk -F',' -v r="$repo" '$2 == r {print $3; exit 0}' "$repo_mapping"
}

manifest_suffix_lookup() {
  local manifest="$1"
  local suffix="$2"
  awk -v want_suffix="$suffix" '
    {
      key = $1
      if (length(key) >= length(want_suffix) &&
          substr(key, length(key) - length(want_suffix) + 1) == want_suffix) {
        $1 = ""
        sub(/^ /, "")
        print
        exit 0
      }
    }
  ' "$manifest"
}

manifest_repo_suffix_lookup() {
  local manifest="$1"
  local repo="$2"
  local rest="$3"
  awk -v want_repo="$repo" -v want_rest="$rest" '
    {
      key = $1
      slash = index(key, "/")
      if (!slash) {
        next
      }
      repo = substr(key, 1, slash - 1)
      candidate = substr(key, slash + 1)
      if ((repo == want_repo || repo ~ (want_repo "$")) && candidate == want_rest) {
        $1 = ""
        sub(/^ /, "")
        print
        exit 0
      }
    }
  ' "$manifest"
}

resolve_runfile_suffix() {
  local suffix="$1"
  local runfiles_dir="${RUNFILES_DIR:-}"
  local test_srcdir="${TEST_SRCDIR:-}"
  local manifest="${RUNFILES_MANIFEST_FILE:-}"

  if [[ -z "$runfiles_dir" && -n "${BIN_ABS:-}" && -d "${BIN_ABS}.runfiles" ]]; then
    runfiles_dir="${BIN_ABS}.runfiles"
  fi
  if [[ -z "$test_srcdir" && -n "${BIN_ABS:-}" && -d "${BIN_ABS}.runfiles" ]]; then
    test_srcdir="${BIN_ABS}.runfiles"
  fi
  if [[ -z "$manifest" && -n "${BIN_ABS:-}" && -f "${BIN_ABS}.runfiles_manifest" ]]; then
    manifest="${BIN_ABS}.runfiles_manifest"
  fi

  if [[ -n "$runfiles_dir" ]]; then
    local resolved=""
    resolved="$(find "$runfiles_dir" -path "*/$suffix" -print -quit 2>/dev/null || true)"
    if [[ -n "$resolved" ]]; then
      echo "$resolved"
      return 0
    fi
  fi
  if [[ -n "$test_srcdir" ]]; then
    local resolved=""
    resolved="$(find "$test_srcdir" -path "*/$suffix" -print -quit 2>/dev/null || true)"
    if [[ -n "$resolved" ]]; then
      echo "$resolved"
      return 0
    fi
  fi
  if [[ -n "$manifest" && -f "$manifest" ]]; then
    local resolved=""
    resolved="$(manifest_suffix_lookup "$manifest" "$suffix")"
    if [[ -n "$resolved" ]]; then
      echo "$resolved"
      return 0
    fi
  fi
  return 1
}

resolve_runfile() {
  local path="$1"
  local runfiles_dir="${RUNFILES_DIR:-}"
  local test_srcdir="${TEST_SRCDIR:-}"
  local manifest="${RUNFILES_MANIFEST_FILE:-}"
  local workspace="${TEST_WORKSPACE:-}"
  local repo_mapping=""

  if [[ -z "$runfiles_dir" && -n "${BIN_ABS:-}" && -d "${BIN_ABS}.runfiles" ]]; then
    runfiles_dir="${BIN_ABS}.runfiles"
  fi
  if [[ -z "$test_srcdir" && -n "${BIN_ABS:-}" && -d "${BIN_ABS}.runfiles" ]]; then
    test_srcdir="${BIN_ABS}.runfiles"
  fi
  if [[ -z "$manifest" && -n "${BIN_ABS:-}" && -f "${BIN_ABS}.runfiles_manifest" ]]; then
    manifest="${BIN_ABS}.runfiles_manifest"
  fi
  if [[ -n "$runfiles_dir" && -f "${runfiles_dir}/_repo_mapping" ]]; then
    repo_mapping="${runfiles_dir}/_repo_mapping"
  elif [[ -n "$manifest" && -f "$manifest" ]]; then
    repo_mapping="$(manifest_lookup "$manifest" "_repo_mapping" || true)"
  elif [[ -n "${BIN_ABS:-}" && -f "${BIN_ABS}.repo_mapping" ]]; then
    repo_mapping="${BIN_ABS}.repo_mapping"
  fi

  if [[ -n "$runfiles_dir" && -e "${runfiles_dir}/${path}" ]]; then
    echo "${runfiles_dir}/${path}"
    return 0
  fi
  if [[ -n "$runfiles_dir" && -n "$workspace" && -e "${runfiles_dir}/${workspace}/${path}" ]]; then
    echo "${runfiles_dir}/${workspace}/${path}"
    return 0
  fi
  if [[ -n "$runfiles_dir" && -n "$repo_mapping" && -f "$repo_mapping" ]]; then
    local repo="${path%%/*}"
    local rest="${path#*/}"
    if [[ "$repo" != "$path" ]]; then
      local mapped=""
      mapped="$(repo_map_lookup "$repo_mapping" "$repo")"
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
    resolved="$(manifest_lookup "$manifest" "$path")"
    if [[ -n "$resolved" ]]; then
      echo "$resolved"
      return 0
    fi
    if [[ -n "$workspace" ]]; then
      resolved="$(manifest_lookup "$manifest" "$workspace/$path")"
      if [[ -n "$resolved" ]]; then
        echo "$resolved"
        return 0
      fi
    fi
    if [[ -n "$repo_mapping" && -f "$repo_mapping" ]]; then
      local repo="${path%%/*}"
      local rest="${path#*/}"
      if [[ "$repo" != "$path" ]]; then
        local mapped=""
        mapped="$(repo_map_lookup "$repo_mapping" "$repo")"
        if [[ -n "$mapped" ]]; then
          resolved="$(manifest_lookup "$manifest" "$mapped/$rest")"
          if [[ -n "$resolved" ]]; then
            echo "$resolved"
            return 0
          fi
        fi
      fi
    fi
    local repo="${path%%/*}"
    local rest="${path#*/}"
    if [[ "$repo" != "$path" ]]; then
      resolved="$(manifest_repo_suffix_lookup "$manifest" "$repo" "$rest")"
      if [[ -n "$resolved" ]]; then
        echo "$resolved"
        return 0
      fi
    fi
  fi
  return 1
}
