#!/bin/bash
# Builds the C runtime for a program ghul-llvm links dynamically against the
# system's glibc (ghul-llvm --dynamic), into out/lib/<arch>-unknown-linux-gnu/:
#
#   libghul.a libtommath.a libgc.a libbacktrace.a libunwind.a
#   libclang_rt.builtins.a ghul.ld ghul-start.o
#
# Each is built with the build environment's gcc against its glibc, so a
# program needs no newer glibc than that environment has (2.35). The archives
# are built without the stack protector and without _FORTIFY_SOURCE, which
# would refer to libc_nonshared.a and to newer glibc symbols, and
# ghul-start.o, built from libghul/start/, stands in for the C library's
# start files and for what libc_nonshared.a holds. None of glibc's own files
# ships: a dynamic link takes libc.so.6 and libm.so.6 from the machine it
# runs on. Run after build/tool.sh. The collector, the unwinder, libtommath
# and libbacktrace are built once and cached; libghul and the start object
# are built every time.

set -euo pipefail

. "$(dirname "$0")/common.sh"

gnu_triple="${TRIPLE/-musl/-gnu}"
gnu_sysroot="${cache}/sysroot-${gnu_triple}-$(cat "${root}/build/versions.sh" "${root}/build/gnu.sh" | sha256sum | cut -c1-12)"
target="${out}/lib/${gnu_triple}"

# Code for a fixed load address, as ghul-llvm generates a dynamically linked
# program's own code, with unwind tables so an exception can be thrown
# through every frame.
common_flags=(
    -O2
    -fno-pie
    -fno-stack-protector
    -U_FORTIFY_SOURCE
    -ffunction-sections
    -fdata-sections
    -funwind-tables
)

# Ubuntu's gcc marks x86-64 code for control-flow protection by default,
# which a program linked with the start object would then not have.
if [ "$(uname -m)" = x86_64 ]; then
    common_flags+=(-fcf-protection=none)
fi

if [ ! -f "${gnu_sysroot}/.complete" ]; then
    build="${work}/gnu"

    rm -rf "${build}" "${gnu_sysroot}"
    mkdir -p "${build}" "${gnu_sysroot}/include"

    unpack() {
        local archive="$1" directory="$2"
        mkdir -p "${directory}"
        tar -xf "${archive}" -C "${directory}" --strip-components=1
    }

    # The collector, single-threaded as for the static build. Built for a
    # dynamically linked program, it finds the data segments of the shared
    # libraries the program loads, so their globals are roots.
    unpack "$(fetch "${BDWGC_URL}" "${BDWGC_SHA256}")" "${build}/gc"
    (
        cd "${build}/gc"
        CFLAGS="${common_flags[*]}" LDFLAGS="-no-pie" ./configure \
            --prefix="${build}/gc-install" \
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
    cp "${build}/gc-install/lib/libgc.a" "${gnu_sysroot}/"
    cp -r "${build}/gc-install/include/." "${gnu_sysroot}/include/"

    # The unwinder, from LLVM's source, as for the static build.
    mkdir -p "${build}/llvm"
    tar -xJf "$(fetch "${LLVM_SOURCE_URL}" "${LLVM_SOURCE_SHA256}")" -C "${build}/llvm" \
        "llvm-project-${LLVM_VERSION}.src/libunwind"
    unwind_source="${build}/llvm/llvm-project-${LLVM_VERSION}.src/libunwind"
    unwind_flags=(
        "${common_flags[@]}"
        -I "${unwind_source}/include"
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

    ar rcsD "${gnu_sysroot}/libunwind.a" "${unwind_objects[@]}"
    cp "${unwind_source}/include/unwind.h" "${unwind_source}/include/libunwind.h" "${unwind_source}/include/__libunwind_config.h" "${gnu_sysroot}/include/"
    cp "${unwind_source}/include/"unwind_*.h "${gnu_sysroot}/include/"

    # The big integers, allocating from libghul's allocator.
    unpack "$(fetch "${LIBTOMMATH_URL}" "${LIBTOMMATH_SHA256}")" "${build}/tommath"
    tommath_objects=()

    for source in "${build}/tommath"/bn_*.c; do
        object="${source%.c}.o"
        gcc "${common_flags[@]}" \
            -DMP_MALLOC='$libghul_mp_malloc' \
            -DMP_REALLOC='$libghul_mp_realloc' \
            -DMP_CALLOC='$libghul_mp_calloc' \
            -DMP_FREE='$libghul_mp_free' \
            -I "${build}/tommath" -c "${source}" -o "${object}"
        tommath_objects+=("${object}")
    done

    ar rcsD "${gnu_sysroot}/libtommath.a" "${tommath_objects[@]}"
    cp "${build}/tommath/tommath.h" "${gnu_sysroot}/include/"

    # The symbolizer for backtraces.
    unpack "$(fetch "${LIBBACKTRACE_URL}" "${LIBBACKTRACE_SHA256}")" "${build}/backtrace"
    (
        cd "${build}/backtrace"
        CFLAGS="${common_flags[*]}" LDFLAGS="-no-pie" ./configure --disable-shared --enable-static --with-pic=no >/dev/null
        make -j "${jobs}" libbacktrace.la >/dev/null
        cp .libs/libbacktrace.a "${gnu_sysroot}/"
        cp backtrace.h "${gnu_sysroot}/include/"
    )

    cp "${llvm_prefix}/${LLVM_BUILTINS}" "${gnu_sysroot}/libclang_rt.builtins.a"

    rm -rf "${build}"
    touch "${gnu_sysroot}/.complete"
fi

rm -rf "${target}"
mkdir -p "${target}"
cp "${gnu_sysroot}"/*.a "${target}/"

# libghul, as libghul.sh builds it for the static runtime.
build="${work}/libghul-gnu"
rm -rf "${build}"
mkdir -p "${build}"

flags=(
    "${common_flags[@]}"
    -std=c11
    -D_GNU_SOURCE
    -Wall
    -Wextra
    -Werror
    -fno-builtin
    -I "${root}/libghul/include"
    -I "${gnu_sysroot}/include"
)

objects=()

for source in "${root}"/libghul/src/*.c; do
    object="${build}/$(basename "${source}" .c).o"
    gcc "${flags[@]}" -c "${source}" -o "${object}"
    objects+=("${object}")
done

ar rcsD "${target}/libghul.a" "${objects[@]}"
cp "${root}/libghul/ghul.ld" "${target}/ghul.ld"

# The start object: the entry point, and what libc_nonshared.a would give.
gcc "${common_flags[@]}" -c "${root}/libghul/start/start-$(uname -m).S" -o "${build}/start.o"
gcc "${flags[@]}" -fno-function-sections -fno-data-sections -c "${root}/libghul/start/runtime.c" -o "${build}/runtime.o"
ld -r "${build}/start.o" "${build}/runtime.o" -o "${target}/ghul-start.o"

# Nothing may refer to the parts of glibc that only its development package
# installs, or to a fortified function.
if nm -u "${target}"/*.a "${target}/ghul-start.o" 2>/dev/null | grep -E ' (__stack_chk_fail_local|__[a-z_]+_chk|pthread_atfork)$'; then
    echo "the gnu runtime refers to libc_nonshared.a or a fortified function" >&2
    exit 1
fi

echo "built ${target}"
