#!/bin/bash
# Builds the ghūl runtime, libghulrt.a, from runtime/ into out/lib/<triple>/,
# with musl-gcc against the collector's headers. Run after build/tool.sh,
# which lays out out/lib/<triple>/ with the rest of the C runtime.

set -euo pipefail

. "$(dirname "$0")/common.sh"

build="${work}/runtime"
target="${out}/lib/${TRIPLE}"

rm -rf "${build}"
mkdir -p "${build}" "${target}"

# Code generation for a fixed load address, as ghul-llvm generates the
# program's own code, so the two link into one static executable, and with
# unwind tables, so an exception can be thrown through the runtime's frames.
flags=(
    -std=c11
    -D_GNU_SOURCE
    -O2
    -Wall
    -Wextra
    -Werror
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -funwind-tables
    -I "${root}/runtime/include"
    -I "${sysroot}/include"
)

objects=()

for source in "${root}"/runtime/src/*.c; do
    object="${build}/$(basename "${source}" .c).o"
    "${musl_prefix}/bin/musl-gcc" "${flags[@]}" -c "${source}" -o "${object}"
    objects+=("${object}")
done

rm -f "${target}/libghulrt.a"
ar rcsD "${target}/libghulrt.a" "${objects[@]}"

echo "built ${target}/libghulrt.a"
