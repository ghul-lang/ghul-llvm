#!/bin/bash
# Runs a command inside the build environment described by build/Dockerfile,
# with the repository mounted at /work and the caller's user and group, so
# that what the command writes is owned by the caller.
#
#   build/docker.sh build/all.sh
#
# The image is tagged with a hash of the Dockerfile, so editing it builds a
# new image rather than reusing a stale one.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
hash="$(sha256sum "${root}/build/Dockerfile" | cut -c1-12)"
image="ghul-llvm-build:${hash}"

if ! docker image inspect "${image}" >/dev/null 2>&1; then
    docker build -q -t "${image}" -f "${root}/build/Dockerfile" "${root}/build" >/dev/null
fi

tty=()
if [ -t 0 ] && [ -t 1 ]; then
    tty=(-it)
fi

exec docker run --rm "${tty[@]}" \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e JOBS \
    -e GHUL_LLVM_VERSION \
    -v "${root}:/work" \
    -w /work \
    "${image}" \
    "$@"
