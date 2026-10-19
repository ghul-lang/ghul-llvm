#!/bin/bash
# Builds the cell host, out/host/bin/ghul-host, with the container's own gcc and
# glibc. Run it in debian:bookworm (build/host-docker.sh does), so the host
# needs no glibc newer than that image's:
#
#   build/host.sh
#
# The host is a glibc executable with libghul, the collector, the unwinder and
# the big integer library linked into it whole and exported, which is what a
# cell, a shared object that carries none of them, binds to when it is loaded.

set -euo pipefail

. "$(dirname "$0")/common.sh"

build="${work}/host"
prefix="${build}/prefix"
target="${out}/host/bin"

rm -rf "${build}"
mkdir -p "${prefix}/include" "${prefix}/lib" "${target}"

unpack() {
    local archive="$1" directory="$2"
    mkdir -p "${directory}"
    tar -xf "${archive}" -C "${directory}" --strip-components=1
}

# The collector, single-threaded as for the static build. The collector reads
# the data segments of the objects dlopen loads when it collects, so a cell's
# globals are roots.
unpack "$(fetch "${BDWGC_URL}" "${BDWGC_SHA256}")" "${build}/gc"

(
    cd "${build}/gc"
    CFLAGS="-O2 -fPIC" ./configure \
        --prefix="${prefix}" \
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

# The unwinder, from LLVM's source, built as for the static libraries.
mkdir -p "${build}/llvm"
tar -xJf "$(fetch "${LLVM_SOURCE_URL}" "${LLVM_SOURCE_SHA256}")" -C "${build}/llvm" \
    "llvm-project-${LLVM_VERSION}.src/libunwind"

unwind_source="${build}/llvm/llvm-project-${LLVM_VERSION}.src/libunwind"

unwind_flags=(
    -I "${unwind_source}/include"
    -O2
    -fPIC
    -funwind-tables
    -D_LIBUNWIND_HAS_NO_THREADS
    -D_LIBUNWIND_IS_NATIVE_ONLY
    -D_GNU_SOURCE
)

unwind_objects=()

for source in libunwind.cpp UnwindLevel1.c UnwindLevel1-gcc-ext.c UnwindRegistersRestore.S UnwindRegistersSave.S; do
    object="${build}/${source}.o"

    case "${source}" in
    *.cpp) g++ "${unwind_flags[@]}" -std=c++17 -fno-exceptions -fno-rtti -c "${unwind_source}/src/${source}" -o "${object}" ;;
    *.c) gcc "${unwind_flags[@]}" -std=c99 -c "${unwind_source}/src/${source}" -o "${object}" ;;
    *.S) gcc "${unwind_flags[@]}" -c "${unwind_source}/src/${source}" -o "${object}" ;;
    esac

    unwind_objects+=("${object}")
done

ar rcsD "${prefix}/lib/libunwind.a" "${unwind_objects[@]}"
cp "${unwind_source}/include/unwind.h" "${unwind_source}/include/libunwind.h" "${unwind_source}/include/__libunwind_config.h" "${prefix}/include/"
cp "${unwind_source}/include/"unwind_*.h "${prefix}/include/"

# The big integers, allocating from libghul's allocator as for the static
# build.
unpack "$(fetch "${LIBTOMMATH_URL}" "${LIBTOMMATH_SHA256}")" "${build}/tommath"

tommath_objects=()

for source in "${build}/tommath"/bn_*.c; do
    object="${source%.c}.o"
    gcc -O2 -fPIC -funwind-tables \
        -DMP_MALLOC='$libghul_mp_malloc' \
        -DMP_REALLOC='$libghul_mp_realloc' \
        -DMP_CALLOC='$libghul_mp_calloc' \
        -DMP_FREE='$libghul_mp_free' \
        -I "${build}/tommath" -c "${source}" -o "${object}"
    tommath_objects+=("${object}")
done

ar rcsD "${prefix}/lib/libtommath.a" "${tommath_objects[@]}"
cp "${build}/tommath/tommath.h" "${prefix}/include/"

# The symbolizer for backtraces.
unpack "$(fetch "${LIBBACKTRACE_URL}" "${LIBBACKTRACE_SHA256}")" "${build}/backtrace"

(
    cd "${build}/backtrace"
    CFLAGS="-O2 -fPIC -funwind-tables" ./configure --prefix="${prefix}" --disable-shared --enable-static >/dev/null
    make -j "${jobs}" libbacktrace.la >/dev/null
    cp .libs/libbacktrace.a "${prefix}/lib/"
    cp backtrace.h "${prefix}/include/"
)

# libghul. Its own main becomes an ordinary function the host does not call:
# the host starts the program itself, once for each cell.
flags=(
    -std=c11
    -D_GNU_SOURCE
    -O2
    -Wall
    -Wextra
    -Werror
    -fno-builtin
    -fPIC
    -ffunction-sections
    -fdata-sections
    -funwind-tables
    -I "${root}/libghul/include"
    -I "${root}/libghul/src"
    -I "${prefix}/include"
)

objects=()

for source in "${root}"/libghul/src/*.c; do
    object="${build}/$(basename "${source}" .c).o"
    extra=()

    if [ "$(basename "${source}")" = entry.c ]; then
        extra=(-Dmain=ghul_standalone_main)
    fi

    gcc "${flags[@]}" "${extra[@]}" -c "${source}" -o "${object}"
    objects+=("${object}")
done

ar rcsD "${prefix}/lib/libghul.a" "${objects[@]}"

gcc "${flags[@]}" -I "${root}/libghul/src" -c "${root}/host/host.c" -o "${build}/host.o"

# Whole archives, so that everything a cell may call is in the executable,
# and -rdynamic, so that it is in the dynamic symbol table.
gcc -rdynamic -o "${target}/ghul-host" "${build}/host.o" \
    -Wl,--whole-archive \
    "${prefix}/lib/libghul.a" "${prefix}/lib/libtommath.a" "${prefix}/lib/libgc.a" "${prefix}/lib/libunwind.a" \
    -Wl,--no-whole-archive \
    "${prefix}/lib/libbacktrace.a" \
    -ldl -lm

echo "built ${target}/ghul-host"
