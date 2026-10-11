#!/bin/bash
# Builds the C runtime that executables are linked against: musl's start
# files and static C library, and LLVM's compiler-rt builtins for the helper
# routines code generation can call (128-bit division, for instance). This is
# what ships in the release archive's lib/<triple> directory.
#
# Does nothing when the sysroot is already there.

set -euo pipefail

. "$(dirname "$0")/common.sh"

if [ -f "${sysroot}/.complete" ]; then
    echo "sysroot already built in ${sysroot}"
    exit 0
fi

llvm_release

archive="$(fetch "${MUSL_URL}" "${MUSL_SHA256}")"
build="${work}/musl-${MUSL_VERSION}"
staging="${work}/musl-${MUSL_VERSION}-install"

rm -rf "${build}" "${staging}" "${sysroot}"
mkdir -p "${work}"
tar -xzf "${archive}" -C "${work}"

(
    cd "${build}"

    # musl's own build, with the system C compiler: no shared library, since
    # every executable is linked statically.
    ./configure --prefix="${staging}" --disable-shared >/dev/null
    make -j "${jobs}" >/dev/null
    make install >/dev/null
)

mkdir -p "${sysroot}"

for file in crt1.o crti.o crtn.o libc.a; do
    cp "${staging}/lib/${file}" "${sysroot}/${file}"
done

cp "${llvm_prefix}/${LLVM_BUILTINS}" "${sysroot}/libclang_rt.builtins.a"
cp "${build}/COPYRIGHT" "${sysroot}/musl-COPYRIGHT"

rm -rf "${build}" "${staging}"
touch "${sysroot}/.complete"
