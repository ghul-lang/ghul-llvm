#!/bin/bash
# Builds libghul, libghul.a, from libghul/ into out/lib/<triple>/: an archive
# of LLVM bitcode, one module for each source file, compiled with LLVM's own
# clang against musl's and the collector's headers. Run after build/tool.sh,
# which lays out out/lib/<triple>/ with the rest of the C runtime.
#
# ghul-llvm links the members the program needs into its module before
# optimising it, as a static linker takes an archive's members, so libghul's
# functions inline into the program and are optimised with it.

set -euo pipefail

. "$(dirname "$0")/common.sh"

llvm_tools

build="${work}/libghul"
target="${out}/lib/${TRIPLE}"
clang="${llvm_tools_prefix}/bin/clang"

rm -rf "${build}"
mkdir -p "${build}" "${target}"

# Only the headers named here are read: clang's own, musl's and the
# collector's, never the host's. Unwind tables are kept, and functions are
# not taken to be unable to unwind, so an exception can be thrown through
# libghul's frames once they are optimised with the program's.
flags=(
    --target="${TRIPLE}"
    -nostdinc
    -isystem "${llvm_tools_prefix}/lib/clang/${LLVM_VERSION%%.*}/include"
    -isystem "${musl_prefix}/include"
    -isystem "${sysroot}/include"
    -I "${root}/libghul/include"
    -std=c11
    -D_GNU_SOURCE
    -O2
    -Wall
    -Wextra
    -Werror
    -fno-pie
    -funwind-tables
    -fexceptions
    -emit-llvm
)

modules=()

for source in "${root}"/libghul/src/*.c; do
    module="${build}/$(basename "${source}" .c).bc"
    "${clang}" "${flags[@]}" -c "${source}" -o "${module}"
    modules+=("${module}")
done

rm -f "${target}/libghul.a"
ar rcD "${target}/libghul.a" "${modules[@]}"

# The linker script that keeps the fault map, read by libghul/src/null.c.
cp "${root}/libghul/ghul.ld" "${target}/ghul.ld"

echo "built ${target}/libghul.a"
