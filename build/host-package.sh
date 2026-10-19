#!/bin/bash
# Packs the cell host in out/host/ into its release archive:
#
#   dist/ghul-llvm-host-<arch>-unknown-linux-gnu.tar.gz
#   dist/ghul-llvm-host-<arch>-unknown-linux-gnu.tar.gz.sha256
#
# The archive unpacks to a single directory holding bin/ghul-host and the
# licences of what is linked into it. Run after build/host.sh.

set -euo pipefail

. "$(dirname "$0")/common.sh"

host_triple="${TRIPLE/-musl/-gnu}"
name="ghul-llvm-host-${host_triple}"
staging="${work}/host-package/${name}"
build="${work}/host"

if [ ! -x "${out}/host/bin/ghul-host" ]; then
    echo "no host in ${out}/host: run build/host.sh first" >&2
    exit 1
fi

rm -rf "${work}/host-package"
mkdir -p "${staging}/bin" "${staging}/licenses"

cp "${out}/host/bin/ghul-host" "${staging}/bin/"

cp "${root}/LICENSE" "${staging}/licenses/ghul-llvm-LICENSE"
cp "${root}/licenses/LLVM-LICENSE.TXT" "${staging}/licenses/LLVM-LICENSE.TXT"
cp "${build}/gc/README.QUICK" "${staging}/licenses/bdwgc-README.QUICK"
cp "${build}/tommath/LICENSE" "${staging}/licenses/libtommath-LICENSE"
cp "${build}/backtrace/LICENSE" "${staging}/licenses/libbacktrace-LICENSE"

mkdir -p "${dist}"
archive="${dist}/${name}.tar.gz"

tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
    -C "${work}/host-package" -cf - "${name}" | gzip -9n >"${archive}"

(cd "${dist}" && sha256sum "$(basename "${archive}")" >"$(basename "${archive}").sha256")

echo "packed ${archive}"
