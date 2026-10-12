#!/bin/bash
# Builds libghul, libghul.a, from libghul/ into out/lib/<triple>/,
# with musl-gcc against the collector's headers. Run after build/tool.sh,
# which lays out out/lib/<triple>/ with the rest of the C runtime.

set -euo pipefail

. "$(dirname "$0")/common.sh"

build="${work}/libghul"
target="${out}/lib/${TRIPLE}"

rm -rf "${build}"
mkdir -p "${build}" "${target}"

# Code generation for a fixed load address, as ghul-llvm generates the
# program's own code, so the two link into one static executable, and with
# unwind tables, so an exception can be thrown through libghul's frames.
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
    -I "${root}/libghul/include"
    -I "${sysroot}/include"
)

objects=()

for source in "${root}"/libghul/src/*.c; do
    object="${build}/$(basename "${source}" .c).o"
    "${musl_prefix}/bin/musl-gcc" "${flags[@]}" -c "${source}" -o "${object}"
    objects+=("${object}")
done

rm -f "${target}/libghul.a"
ar rcsD "${target}/libghul.a" "${objects[@]}"

# The linker script that keeps the fault map, read by libghul/src/null.c.
cp "${root}/libghul/ghul.ld" "${target}/ghul.ld"

echo "built ${target}/libghul.a"
