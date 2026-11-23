#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Verify BR §17.8 and ROADMAP P3.9: two independent third-party integrations
# build and run from an installed ztick-sdk package without modifying this repo.
set -euo pipefail

cmake_bin="${CMAKE:-cmake}"
ctest_bin="${CTEST:-ctest}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="${1:-${TMPDIR:-/tmp}/ztick-external-apps-check}"
sdk_build="${work_dir}/sdk-build"
prefix="${work_dir}/prefix"
apps_build="${work_dir}/apps-build"

rm -rf "${work_dir}"
mkdir -p "${work_dir}"

"${cmake_bin}" -S "${repo_root}" -B "${sdk_build}" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DZTICK_BUILD_EXAMPLES=OFF >/dev/null
"${cmake_bin}" --build "${sdk_build}" --parallel >/dev/null
"${cmake_bin}" --install "${sdk_build}" --prefix "${prefix}" >/dev/null

"${cmake_bin}" -S "${repo_root}/tests/sdk_external_apps" -B "${apps_build}" \
  -DCMAKE_PREFIX_PATH="${prefix}" >/dev/null
"${cmake_bin}" --build "${apps_build}" --parallel >/dev/null
"${ctest_bin}" --test-dir "${apps_build}" --output-on-failure

echo "External apps check passed (prefix: ${prefix})"
