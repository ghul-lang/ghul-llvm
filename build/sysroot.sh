#!/bin/bash
# Builds what every executable is linked against apart from libghul
# itself: musl's start files and static C library, LLVM's
# compiler-rt builtins for the helper routines code generation can call
# (128-bit division, for instance), the Boehm garbage collector, built
# single-threaded against musl, and LLVM's libunwind, which exceptions are
# thrown through. This is what ships in the release archive's
# lib/<triple> directory beside libghul.a.
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
# musl-gcc and the headers are what the collector and libghul are
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
mkdir -p "${sysroot}"
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

# libunwind, compiled directly rather than through LLVM's runtimes build:
# its sources need only C headers, which musl supplies, and the build
# defines nothing beyond what is spelt out here. Single-threaded, as the
# collector is.
source_archive="$(fetch "${LLVM_SOURCE_URL}" "${LLVM_SOURCE_SHA256}")"
unwind_build="${work}/libunwind-${LLVM_VERSION}"
unwind_source="${unwind_build}/llvm-project-${LLVM_VERSION}.src/libunwind"

rm -rf "${unwind_build}"
mkdir -p "${unwind_build}"
tar -xJf "${source_archive}" -C "${unwind_build}" "llvm-project-${LLVM_VERSION}.src/libunwind"

unwind_flags=(
    -nostdinc
    -isystem "${musl_prefix}/include"
    -isystem "$(gcc -print-file-name=include)"
    -I "${unwind_source}/include"
    -O2
    -fno-pie
    -funwind-tables
    -D_LIBUNWIND_HAS_NO_THREADS
    -D_LIBUNWIND_IS_NATIVE_ONLY
    -D_GNU_SOURCE
)

unwind_objects=()

for source in libunwind.cpp UnwindLevel1.c UnwindLevel1-gcc-ext.c UnwindRegistersRestore.S UnwindRegistersSave.S; do
    object="${unwind_build}/${source}.o"

    case "${source}" in
    *.cpp) g++ "${unwind_flags[@]}" -nostdinc++ -std=c++17 -fno-exceptions -fno-rtti -c "${unwind_source}/src/${source}" -o "${object}" ;;
    *.c) gcc "${unwind_flags[@]}" -std=c99 -c "${unwind_source}/src/${source}" -o "${object}" ;;
    *.S) gcc "${unwind_flags[@]}" -c "${unwind_source}/src/${source}" -o "${object}" ;;
    esac

    unwind_objects+=("${object}")
done

mkdir -p "${sysroot}/include"

ar rcsD "${sysroot}/libunwind.a" "${unwind_objects[@]}"
cp "${unwind_source}/include/unwind.h" "${unwind_source}/include/__libunwind_config.h" "${sysroot}/include/"
cp "${unwind_source}/include/"unwind_*.h "${sysroot}/include/"
rm -rf "${unwind_build}"

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
