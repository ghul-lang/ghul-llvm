#!/bin/bash
# Builds everything, in the build environment, from a clean checkout:
#
#   build/all.sh [version]
#
# fetches and unpacks LLVM's release, builds the C runtime and the
# collector, builds ghul-llvm and the ghūl runtime into out/, and packs the release archive into dist/. Then runs
# the smoke test against the archive as a user would unpack it. Inputs are
# cached under .cache/, so a second run only rebuilds ghul-llvm.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
version="${1:-}"

"${root}/build/docker.sh" bash -c "
    set -euo pipefail
    build/sysroot.sh
    build/tool.sh ${version}
    build/runtime.sh
    build/package.sh
"

. "${root}/build/versions.sh"

unpacked="$(mktemp -d)"
trap 'rm -rf "${unpacked}"' EXIT

tar -xzf "${root}/dist/ghul-llvm-${TRIPLE}.tar.gz" -C "${unpacked}"
"${root}/tests/smoke.sh" "${unpacked}/ghul-llvm-${TRIPLE}" ${version}
