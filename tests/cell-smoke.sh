#!/bin/bash
# Checks the cell host against ghul-llvm: that a cell built with --emit=cell
# is loaded and run by the host, takes the host's names rather than carrying
# its own, and ends with the status its entry returned.
#
#   tests/cell-smoke.sh <ghul-llvm install directory> <host install directory>
#
# The first holds bin/ghul-llvm, as out/ and the release archive do; the
# second holds bin/ghul-host.

set -euo pipefail

tests="$(cd "$(dirname "$0")" && pwd)"

tool="$(cd "$1" && pwd)/bin/ghul-llvm"
host="$(cd "$2" && pwd)/bin/ghul-host"

scratch="$(mktemp -d)"
trap 'rm -rf "${scratch}"' EXIT

"${tool}" --emit=cell "${tests}/cell.ll" -o "${scratch}/cell.so"

# A cell carries no C library of its own: whatever it needs, the host
# supplies.
if readelf -d "${scratch}/cell.so" | grep -q NEEDED; then
    echo "FAILED: the cell names a library it needs" >&2
    exit 1
fi

status=0
output="$("${host}" "${scratch}/cell.so")" || status=$?

if [ "${output}" != "hello from a cell" ]; then
    echo "FAILED: the cell printed '${output}'" >&2
    exit 1
fi

if [ "${status}" -ne 7 ]; then
    echo "FAILED: the host exited ${status}, expected the entry's 7" >&2
    exit 1
fi

# Two cells in one process, and the arguments after --.
"${host}" "${scratch}/cell.so" "${scratch}/cell.so" -- one two >"${scratch}/twice" || true

if [ "$(grep -c 'hello from a cell' "${scratch}/twice")" -ne 2 ]; then
    echo "FAILED: the host did not run both cells" >&2
    exit 1
fi

# A cell with no entry point, and a cell that calls into it.
"${tool}" --emit=cell "${tests}/cell-library.ll" -o "${scratch}/library.so"
"${tool}" --emit=cell "${tests}/cell-user.ll" -o "${scratch}/user.so"

status=0
"${host}" "${scratch}/library.so" "${scratch}/user.so" || status=$?

if [ "${status}" -ne 42 ]; then
    echo "FAILED: a cell calling into the one before it exited ${status}, expected 42" >&2
    exit 1
fi

# The host kept running, taking a cell at a time: one that throws what nothing
# catches ends that cell and not the session.
"${tool}" --emit=cell "${tests}/cell-throw.ll" -o "${scratch}/throw.so"

mkfifo "${scratch}/commands"

"${host}" --serve "${scratch}/commands" > "${scratch}/served" 2> /dev/null &
server=$!

{
    echo "${scratch}/library.so"
    echo "${scratch}/throw.so"
    echo "${scratch}/cell.so"
    echo "${scratch}/user.so"
    echo quit
} > "${scratch}/commands"

wait "${server}"

expected=$(printf '\001cell-done 0\n\001cell-done 134\nhello from a cell\n\001cell-done 7\n\001cell-done 42\n')

if [ "$(cat "${scratch}/served")" != "${expected}" ]; then
    echo "FAILED: the host in serve mode wrote:" >&2
    cat -v "${scratch}/served" >&2
    exit 1
fi

# A cell that never returns is ended by SIGINT, and the session goes on; a
# SIGINT between cells is ignored.
"${tool}" --emit=cell "${tests}/cell-loop.ll" -o "${scratch}/loop.so"

rm -f "${scratch}/commands"
mkfifo "${scratch}/commands"

exec 5<>"${scratch}/commands"

"${host}" --serve "${scratch}/commands" > "${scratch}/interrupted" 2> /dev/null &
server=$!

sleep 1
kill -INT "${server}"
sleep 0.3

echo "${scratch}/loop.so" >&5
sleep 1
kill -INT "${server}"

echo "${scratch}/cell.so" >&5
echo quit >&5

wait "${server}"
exec 5>&-

expected=$(printf '\001cell-done 130\nhello from a cell\n\001cell-done 7\n')

if [ "$(cat "${scratch}/interrupted")" != "${expected}" ]; then
    echo "FAILED: the host interrupted wrote:" >&2
    cat -v "${scratch}/interrupted" >&2
    exit 1
fi

echo "ok: a cell is loaded and run by the host"
