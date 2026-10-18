#!/bin/bash
# Builds ghul-llvm itself against LLVM's release libraries, into out/.
#
#   build/tool.sh [version]
#
# The version is what `ghul-llvm --version` reports; it defaults to the one
# in VERSION, and CI passes the version of the release being built.

set -euo pipefail

. "$(dirname "$0")/common.sh"

llvm_release

version="${1:-$(tr -d '[:space:]' <"${root}/VERSION")}"
build="${work}/ghul-llvm"

cmake -G Ninja -S "${root}" -B "${build}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${out}" \
    -DLLVM_ROOT="${llvm_prefix}" \
    -DGHUL_LLVM_VERSION="${version}" \
    -DGHUL_LLVM_TRIPLE="${TRIPLE}" \
    -DGHUL_LLVM_BACKEND="${GHUL_LLVM_BACKEND}" \
    -DGHUL_LLVM_CPU="${GHUL_LLVM_CPU}" \
    >/dev/null

ninja -C "${build}" -j "${jobs}"
cmake --install "${build}" >/dev/null

strip "${out}/bin/ghul-llvm"

# The C runtime goes where ghul-llvm looks for it by default, so out/ is a
# working install as it stands.
rm -rf "${out}/lib/${TRIPLE}"
mkdir -p "${out}/lib/${TRIPLE}"
cp "${sysroot}"/*.o "${sysroot}"/*.a "${out}/lib/${TRIPLE}/"

echo "built ${out}/bin/ghul-llvm"
