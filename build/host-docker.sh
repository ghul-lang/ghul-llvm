#!/bin/bash
# Runs build/host.sh in debian:bookworm, the oldest glibc the host is built
# against, with the repository mounted at /work. What it writes is handed back
# to the caller.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cache="${GHUL_LLVM_CACHE:-${root}/.cache}"

mkdir -p "${cache}"

docker run --rm -e GHUL_LLVM_CACHE="${cache}" -e OWNER="$(id -u):$(id -g)" \
    -v "${root}:/work" -v "${cache}:${cache}" -w /work \
    debian:bookworm bash -c '
        set -euo pipefail
        apt-get update -qq >/dev/null
        apt-get install -y -qq --no-install-recommends gcc g++ make curl ca-certificates xz-utils file >/dev/null
        trap "chown -R ${OWNER} /work/out ${GHUL_LLVM_CACHE}" EXIT
        build/host.sh
    '
