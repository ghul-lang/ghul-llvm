#!/bin/bash
# Builds what every executable is linked against apart from the ghūl
# runtime itself: musl's start files and static C library, LLVM's
# compiler-rt builtins for the helper routines code generation can call
# (128-bit division, for instance), and the Boehm garbage collector, built
# single-threaded against musl. This is what ships in the release archive's
# lib/<triple> directory beside libghulrt.a.
#
# Does nothing when the sysroot is already there.

set -euo pipefail

. "$(dirname "$0")/common.sh"

if [ -f "${sysroot}/.complete" ]; then
    echo "sysroot already built in ${sysroot}"
    exit 0
fi

llvm_release

# musl's own build, with the system C compiler: no shared library, since
# every executable is linked statically. It stays installed, because
# musl-gcc and the headers are what the collector and the runtime are
# compiled with.
if [ ! -f "${musl_prefix}/.complete" ]; then
    archive="$(fetch "${MUSL_URL}" "${MUSL_SHA256}")"
    build="${work}/musl-${MUSL_VERSION}"

    rm -rf "${build}" "${musl_prefix}"
    mkdir -p "${work}"
    tar -xzf "${archive}" -C "${work}"

    (
        cd "${build}"
        ./configure --prefix="${musl_prefix}" --disable-shared >/dev/null
        make -j "${jobs}" >/dev/null
        make install >/dev/null
    )

    cp "${build}/COPYRIGHT" "${musl_prefix}/COPYRIGHT"
    rm -rf "${build}"
    touch "${musl_prefix}/.complete"
fi

# The collector: single-threaded, since a ghūl program is, and without the
# Java and C++ support the library also offers.
archive="$(fetch "${BDWGC_URL}" "${BDWGC_SHA256}")"
build="${work}/gc-${BDWGC_VERSION}"
staging="${work}/gc-${BDWGC_VERSION}-install"

rm -rf "${build}" "${staging}" "${sysroot}"
tar -xzf "${archive}" -C "${work}"

(
    cd "${build}"
    CC="${musl_prefix}/bin/musl-gcc" CFLAGS="-O2 -fno-pie" LDFLAGS="-static -no-pie" ./configure \
        --prefix="${staging}" \
        --disable-shared \
        --enable-static \
        --disable-threads \
        --disable-parallel-mark \
        --disable-gcj-support \
        --disable-java-finalization \
        --disable-docs \
        --with-libatomic-ops=none \
        >/dev/null
    make -j "${jobs}" >/dev/null
    make install >/dev/null
)

mkdir -p "${sysroot}/include"

for file in crt1.o crti.o crtn.o libc.a; do
    cp "${musl_prefix}/lib/${file}" "${sysroot}/${file}"
done

cp "${llvm_prefix}/${LLVM_BUILTINS}" "${sysroot}/libclang_rt.builtins.a"
cp "${staging}/lib/libgc.a" "${sysroot}/libgc.a"
cp -r "${staging}/include/." "${sysroot}/include/"
cp "${musl_prefix}/COPYRIGHT" "${sysroot}/musl-COPYRIGHT"
cp "${build}/README.QUICK" "${sysroot}/bdwgc-README.QUICK"

rm -rf "${build}" "${staging}"
touch "${sysroot}/.complete"
