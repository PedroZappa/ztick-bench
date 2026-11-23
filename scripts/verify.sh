#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Run the complete verification gate with CI flags active (ROADMAP P1.14).
# The build uses -DZTICK_WERROR=ON, so -Wall -Wextra -Wpedantic become errors.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

cmake_bin="${CMAKE:-cmake}"
ctest_bin="${CTEST:-ctest}"
build_type="${CMAKE_BUILD_TYPE:-Release}"
ci_build_dir="${CI_BUILD_DIR:-build/ci}"
ci_cmake_args="${CI_CMAKE_ARGS:--DZTICK_WERROR=ON}"

echo "== Formatting =="
make format-check

echo "== Static analysis =="
make cppcheck

echo "== Documentation (warnings are errors) =="
make docs

echo "== CI build: ${build_type}, ${ci_cmake_args} =="
"${cmake_bin}" -S . -B "${ci_build_dir}" -DCMAKE_BUILD_TYPE="${build_type}" ${ci_cmake_args}
"${cmake_bin}" --build "${ci_build_dir}" --parallel
"${ctest_bin}" --test-dir "${ci_build_dir}" --output-on-failure

echo "== SDK consumer =="
make sdk-consumer-check

echo "== External-style apps =="
make external-apps-check

echo "== Sanitizers (ASan + UBSan) =="
make sanitize

echo "== Verification passed: ${build_type} with ${ci_cmake_args} =="
