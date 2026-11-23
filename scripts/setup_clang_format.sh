#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Provision a local clang-format 18 toolchain for hosts without a native binary.
#
# Usage: scripts/setup_clang_format.sh <destination-root>
#
# The formatter version is pinned by AGENTS.md. CI installs clang-format-18
# natively, so this script is a no-op there. On Linux/x86_64 hosts without one,
# the official LLVM 18 Debian packages are downloaded and extracted under
# <destination-root>, outside the repository. Nothing is written to the repo.
set -euo pipefail

root="${1:?usage: setup_clang_format.sh <destination-root>}"
version="${CLANG_FORMAT_VERSION:-18.1.8-18+b1}"
mirror="${CLANG_FORMAT_DEB_MIRROR:-https://deb.debian.org/debian/pool/main}"

bin="${root}/usr/bin/clang-format-18"
libdir="${root}/usr/lib/x86_64-linux-gnu"

# A native pinned binary is preferred; this is the path CI takes.
if command -v clang-format-18 >/dev/null 2>&1; then
  echo "clang-format-18 already available: $(command -v clang-format-18)"
  exit 0
fi

# The fallback is a developer convenience for this host, not a cross-platform
# installer. Other platforms must provide clang-format-18 through their own
# package manager.
if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
  echo "error: no clang-format-18 on PATH; the local fallback supports Linux/x86_64 only" >&2
  echo "Install clang-format 18 with your platform package manager." >&2
  exit 1
fi

# An already-provisioned copy is reused without network access.
if [[ -x "${bin}" ]] && \
   LD_LIBRARY_PATH="${libdir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" "${bin}" --version 2>/dev/null | \
   grep -q 'version 18\.'; then
  echo "Local clang-format 18 already provisioned: ${bin}"
  exit 0
fi

packages=(
  "l/llvm-toolchain-18/clang-format-18_${version}_amd64.deb"
  "l/llvm-toolchain-18/libclang-cpp18_${version}_amd64.deb"
  "l/llvm-toolchain-18/libllvm18_${version}_amd64.deb"
  "z/z3/libz3-4_4.13.3-1_amd64.deb"
)

echo "Provisioning clang-format ${version} under ${root}"
mkdir -p "${root}/debs" "${libdir}"
for package in "${packages[@]}"; do
  deb="${root}/debs/${package##*/}"
  if [[ ! -s "${deb}" ]]; then
    curl -fsSL "${mirror}/${package}" -o "${deb}"
  fi
done

for deb in "${root}"/debs/*.deb; do
  (
    cd "${root}/debs"
    ar x "${deb}"
    tar xf data.tar.* -C "${root}"
    rm -f control.tar.* data.tar.* debian-binary
  )
done

# Debian's LLVM 18 libraries request older SONAMEs than a current Arch host
# provides. clang-format does not call into libedit or libxml2, so pointing the
# requested names at the host's unversioned libraries is safe.
for pair in \
  "libedit.so.2:/usr/lib/libedit.so" \
  "libxml2.so.2:/usr/lib/libxml2.so"; do
  wanted="${pair%%:*}"
  target="${pair##*:}"
  if [[ ! -e "${libdir}/${wanted}" && -e "${target}" ]]; then
    ln -s "${target}" "${libdir}/${wanted}"
  fi
done

if ! LD_LIBRARY_PATH="${libdir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" "${bin}" --version 2>/dev/null | \
   grep -q 'version 18\.'; then
  echo "error: provisioned formatter failed its version check: ${bin}" >&2
  exit 1
fi

echo "clang-format 18 ready: ${bin}"
