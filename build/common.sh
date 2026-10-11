# Sourced by the build scripts: where things go, and fetching pinned inputs.

# A failure inside $(...) has to stop the script too.
shopt -s inherit_errexit

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# shellcheck source=build/versions.sh
. "${root}/build/versions.sh"

# Everything fetched or built from a pinned input. GHUL_LLVM_CACHE moves it,
# so that several checkouts can share one copy of LLVM's release.
cache="${GHUL_LLVM_CACHE:-${root}/.cache}"
downloads="${cache}/downloads"
work="${cache}/build"

# LLVM's release, unpacked down to the headers and static libraries, and the
# C runtime, collector and unwinder executables are linked against. Each is
# named for what it came from, the sysroot by a hash of the scripts that pin
# and build it, so a change to either never reuses an old one.
llvm_prefix="${cache}/${LLVM_RELEASE}"
sysroot="${cache}/sysroot-${TRIPLE}-$(cat "${root}/build/versions.sh" "${root}/build/sysroot.sh" | sha256sum | cut -c1-12)"

# musl, installed with its headers and the musl-gcc wrapper that compiles C
# against it: libghul and the collector are built with it.
musl_prefix="${cache}/musl-${MUSL_VERSION}"

out="${root}/out"
dist="${root}/dist"

jobs="${JOBS:-$(nproc)}"

# fetch <url> <sha256>: downloads <url> into the download cache once, and
# refuses a file whose checksum is not the pinned one. Prints the path.
fetch() {
    local url="$1" sha256="$2"
    local file
    file="${downloads}/$(basename "${url}")"

    mkdir -p "${downloads}"

    if [ ! -f "${file}" ]; then
        curl -fsSL --retry 3 -o "${file}.part" "${url}"
        mv "${file}.part" "${file}"
    fi

    local actual
    actual="$(sha256sum "${file}" | cut -d' ' -f1)"

    if [ "${actual}" != "${sha256}" ]; then
        echo "${file}: checksum ${actual} does not match the pinned ${sha256}" >&2
        rm -f "${file}"
        exit 1
    fi

    echo "${file}"
}

# llvm_release: unpacks the parts of LLVM's release ghul-llvm builds against,
# once. The release also carries clang, MLIR, LLDB and the tools, which are
# several gigabytes nothing here reads.
llvm_release() {
    if [ -f "${llvm_prefix}/.unpacked" ]; then
        return
    fi

    local archive
    archive="$(fetch "${LLVM_URL}" "${LLVM_SHA256}")"

    rm -rf "${llvm_prefix}"
    mkdir -p "${cache}"

    # The release is compressed with a 1GB window, past zstd's default
    # decoding limit.
    zstd --long=31 -dc "${archive}" | tar -x -C "${cache}" \
        --wildcards \
        --exclude="${LLVM_RELEASE}/include/clang*" \
        --exclude="${LLVM_RELEASE}/include/mlir*" \
        --exclude="${LLVM_RELEASE}/include/lldb*" \
        --exclude="${LLVM_RELEASE}/include/flang*" \
        --exclude="${LLVM_RELEASE}/include/polly*" \
        --exclude="${LLVM_RELEASE}/lib/libclang*" \
        --exclude="${LLVM_RELEASE}/lib/libMLIR*" \
        --exclude="${LLVM_RELEASE}/lib/liblldb*" \
        --exclude="${LLVM_RELEASE}/lib/libflang*" \
        --exclude="${LLVM_RELEASE}/lib/libFortran*" \
        --exclude="${LLVM_RELEASE}/lib/libPolly*" \
        "${LLVM_RELEASE}/include/*" \
        "${LLVM_RELEASE}/lib/libLLVM*.a" \
        "${LLVM_RELEASE}/lib/liblld*.a" \
        "${LLVM_RELEASE}/${LLVM_BUILTINS}"

    touch "${llvm_prefix}/.unpacked"
}
