#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Verify BR §17.8: an external consumer can find_package(ztick-sdk) and compile
# against the installed headers without modifying the ztick-bench repository.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="${1:-${TMPDIR:-/tmp}/ztick-sdk-consumer-check}"
sdk_build="${work_dir}/sdk-build"
prefix="${work_dir}/prefix"
consumer_build="${work_dir}/consumer-build"

rm -rf "${work_dir}"
mkdir -p "${work_dir}"

cmake -S "${repo_root}" -B "${sdk_build}" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DZTICK_BUILD_EXAMPLES=OFF >/dev/null
cmake --install "${sdk_build}" --prefix "${prefix}" >/dev/null
cmake -S "${repo_root}/tests/sdk_consumer" -B "${consumer_build}" \
  -DCMAKE_PREFIX_PATH="${prefix}" >/dev/null
cmake --build "${consumer_build}" --parallel >/dev/null

"${consumer_build}/ztick_sdk_consumer_c"
"${consumer_build}/ztick_sdk_consumer_cpp"

echo "SDK consumer check passed (prefix: ${prefix})"
