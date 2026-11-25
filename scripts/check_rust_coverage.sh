#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Generate Rust TUI coverage reports with the same native environment as CTest.
# Environment variables are the single source of truth so Make and CMake share
# one implementation of the coverage command.
set -euo pipefail

# @brief Require a non-empty environment variable.
# @param name Variable name to check.
require_var() {
  local name="$1"
  if [[ -z "${!name:-}" ]]; then
    echo "missing required environment variable: ${name}" >&2
    exit 2
  fi
}

require_var ZTICK_TUI_MANIFEST
require_var ZTICK_CARGO_TARGET_DIR
require_var ZTICK_FRONTEND_LIB_DIR
require_var ZTICK_RUST_COVERAGE_DIR

cargo_bin="${ZTICK_CARGO:-cargo}"
if ! "${cargo_bin}" llvm-cov --version >/dev/null 2>&1; then
  echo "cargo-llvm-cov is required for tui.rust.coverage" >&2
  exit 2
fi

mkdir -p "${ZTICK_RUST_COVERAGE_DIR}"

export CARGO_TARGET_DIR="${ZTICK_CARGO_TARGET_DIR}"
export ZTICK_FRONTEND_LIB_DIR
export LD_LIBRARY_PATH="${ZTICK_FRONTEND_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export CARGO_LLVM_COV_TARGET_DIR="${ZTICK_RUST_COVERAGE_DIR}/llvm-cov-target"

if [[ -n "${ZTICK_TUI_TEST_MODULE:-}" ]]; then
  export ZTICK_TUI_TEST_MODULE
fi

"${cargo_bin}" llvm-cov --manifest-path "${ZTICK_TUI_MANIFEST}" --locked --all-targets \
  --lcov --output-path "${ZTICK_RUST_COVERAGE_DIR}/lcov.info"

report_args=(
  llvm-cov report
  --manifest-path "${ZTICK_TUI_MANIFEST}"
  --locked
  --html
  --output-dir "${ZTICK_RUST_COVERAGE_DIR}"
)
if [[ -n "${ZTICK_RUST_COVERAGE_MIN_LINES:-}" ]]; then
  report_args+=(--fail-under-lines "${ZTICK_RUST_COVERAGE_MIN_LINES}")
fi
"${cargo_bin}" "${report_args[@]}"
