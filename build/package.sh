#!/bin/bash
# Packs the install in out/ into the release archive for this triple:
#
#   dist/ghul-llvm-<triple>.tar.gz
#   dist/ghul-llvm-<triple>.tar.gz.sha256
#
# The archive unpacks to a single ghul-llvm-<triple>/ directory holding
# bin/ghul-llvm, the C runtime under lib/<triple>/, and the licences of
# everything linked into either. Its bytes depend only on its contents, so
# packing the same build twice gives the same archive.

set -euo pipefail

. "$(dirname "$0")/common.sh"

name="ghul-llvm-${TRIPLE}"
staging="${work}/package/${name}"

if [ ! -x "${out}/bin/ghul-llvm" ]; then
    echo "no build in ${out}: run build/tool.sh first" >&2
    exit 1
fi

rm -rf "${work}/package"
mkdir -p "${staging}/bin" "${staging}/lib/${TRIPLE}" "${staging}/licenses"

cp "${out}/bin/ghul-llvm" "${staging}/bin/"
cp "${out}/lib/${TRIPLE}"/* "${staging}/lib/${TRIPLE}/"

# ghul-llvm and libghul, LLVM and LLD (in the tool, and compiler-rt in
# every program it builds), musl, the collector and libbacktrace (in every
# program), and
# the compression libraries
# linked into the tool. The C++ runtime is GCC's, under the runtime library
# exception, which asks for no notice.
cp "${root}/LICENSE" "${staging}/licenses/ghul-llvm-LICENSE"
cp "${root}/licenses/LLVM-LICENSE.TXT" "${staging}/licenses/LLVM-LICENSE.TXT"
cp "${sysroot}/musl-COPYRIGHT" "${staging}/licenses/musl-COPYRIGHT"
cp "${sysroot}/bdwgc-README.QUICK" "${staging}/licenses/bdwgc-README.QUICK"
cp "${sysroot}/libtommath-LICENSE" "${staging}/licenses/libtommath-LICENSE"
cp "${sysroot}/libbacktrace-LICENSE" "${staging}/licenses/libbacktrace-LICENSE"
cp /usr/share/doc/libzstd-dev/copyright "${staging}/licenses/zstd-copyright"
cp /usr/share/doc/zlib1g-dev/copyright "${staging}/licenses/zlib-copyright"

mkdir -p "${dist}"
archive="${dist}/${name}.tar.gz"

tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
    -C "${work}/package" -cf - "${name}" | gzip -9n >"${archive}"

(cd "${dist}" && sha256sum "${name}.tar.gz" >"${name}.tar.gz.sha256")

rm -rf "${work}/package"

echo "packed ${archive} ($(du -h "${archive}" | cut -f1))"
