#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Run the complete verification gate with CI flags active (ROADMAP P1.14).
# The build uses -DZTICK_WERROR=ON, so -Wall -Wextra -Wpedantic become errors.
# Every suite is recorded and a result summary is printed on exit, including
# when a suite fails, so the report always shows what actually ran.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

cmake_bin="${CMAKE:-cmake}"
ctest_bin="${CTEST:-ctest}"
build_type="${CMAKE_BUILD_TYPE:-Release}"
ci_build_dir="${CI_BUILD_DIR:-build/ci}"
ci_cmake_args="${CI_CMAKE_ARGS:--DZTICK_WERROR=ON}"

# Colors follow the Makefile convention: tput is silenced when TERM is unset
# (for example in CI), leaving empty sequences rather than control characters.
B="$(tput bold 2>/dev/null || true)"
RED="$(tput setaf 1 2>/dev/null || true)"
GRN="$(tput setaf 2 2>/dev/null || true)"
YEL="$(tput setaf 3 2>/dev/null || true)"
D="$(tput sgr0 2>/dev/null || true)"

log_dir="$(mktemp -d "${TMPDIR:-/tmp}/ztick-verify.XXXXXX")"
declare -a results=()
step_index=0

# @brief Extract the CTest pass line from one captured suite log.
# @param log Path to the captured output of a suite.
# @return CTest's own pass summary, or an empty string when none was printed.
ctest_detail() {
  local log="$1"
  local line
  line="$(grep -Eo '[0-9]+% tests passed(, [0-9]+ tests failed)? out of [0-9]+' \
    "${log}" | tail -n 1 || true)"
  printf '%s' "${line}"
}

# @brief Store one suite result for the final summary.
# @param label Human-readable suite name.
# @param status Either PASS or FAIL.
# @param detail Optional extra information, such as the CTest pass summary.
record_result() {
  results+=("$1|$2|$3")
}

# @brief Print the recorded suite results and clean up captured logs.
# @note Installed as the EXIT trap so a failing gate is summarised as well.
print_summary() {
  local exit_code=$?
  trap - EXIT

  echo
  echo "${B}${YEL}== Verification summary (${build_type}, ${ci_cmake_args}) ==${D}"
  local entry label status detail width padding color
  for entry in ${results[@]+"${results[@]}"}; do
    IFS='|' read -r label status detail <<< "${entry}"
    width=$((44 - ${#label}))
    if ((width < 1)); then width=1; fi
    padding="$(printf '%*s' "${width}" '' | tr ' ' '.')"
    if [[ "${status}" == "PASS" ]]; then color="${GRN}"; else color="${RED}"; fi
    if [[ -n "${detail}" ]]; then
      printf '  %s %s %s%s%s (%s)\n' "${label}" "${padding}" "${color}" "${status}" "${D}" "${detail}"
    else
      printf '  %s %s %s%s%s\n' "${label}" "${padding}" "${color}" "${status}" "${D}"
    fi
  done

  rm -rf "${log_dir}" || true
  if ((exit_code == 0)); then
    echo "${B}${GRN}== Verification passed: ${build_type} with ${ci_cmake_args} ==${D}"
  else
    echo "${B}${RED}== Verification FAILED: ${build_type} with ${ci_cmake_args} (exit ${exit_code}) ==${D}"
  fi
}
trap print_summary EXIT

# @brief Run one verification suite, capture its output and record the result.
# @param label Suite name shown in the live output and the summary.
# @param kind Detail kind: "ctest" records the CTest pass line, "-" records none.
# @param ... Command and arguments to execute.
# @note Exits with the suite's status on failure so the EXIT trap summarises it.
run_step() {
  local label="$1"
  local kind="$2"
  shift 2

  step_index=$((step_index + 1))
  local log="${log_dir}/$(printf '%02d' "${step_index}").log"
  echo "== ${label} =="

  set +e
  "$@" 2>&1 | tee "${log}"
  local -a pipe_status=("${PIPESTATUS[@]}")
  set -e
  local status="${pipe_status[0]}"
  if ((status == 0)); then status="${pipe_status[1]}"; fi

  local detail=""
  if [[ "${kind}" == "ctest" ]]; then
    detail="$(ctest_detail "${log}")"
  fi
  if ((status == 0)); then
    record_result "${label}" "PASS" "${detail}"
  else
    record_result "${label}" "FAIL" "${detail}"
    exit "${status}"
  fi
}

run_step "Formatting" "-" make format-check
run_step "Static analysis" "-" make cppcheck
run_step "Documentation" "-" make docs
run_step "CI configure" "-" "${cmake_bin}" -S . -B "${ci_build_dir}" \
  -DCMAKE_BUILD_TYPE="${build_type}" ${ci_cmake_args}
run_step "CI build" "-" "${cmake_bin}" --build "${ci_build_dir}" --parallel
run_step "CI tests" "ctest" "${ctest_bin}" --test-dir "${ci_build_dir}" --output-on-failure
run_step "SDK consumer" "-" make sdk-consumer-check
run_step "External-style apps" "ctest" make external-apps-check
run_step "Sanitizers (ASan + UBSan)" "ctest" make sanitize
