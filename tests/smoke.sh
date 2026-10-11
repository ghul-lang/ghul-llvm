#!/bin/bash
# Checks a ghul-llvm install: that it builds programs that run, that it
# reports bad input as a failure, that its output is the same from one run to
# the next, and that both it and what it builds stand on their own.
#
#   tests/smoke.sh [install directory] [expected version]
#
# The install directory holds bin/ghul-llvm and lib/<triple>/, as out/ does
# after build/tool.sh and as the release archive does once unpacked. It
# defaults to out/.

set -euo pipefail

tests="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "${tests}")"

install="$(cd "${1:-${root}/out}" && pwd)"
expected_version="${2:-}"

tool="${install}/bin/ghul-llvm"
scratch="$(mktemp -d)"
trap 'rm -rf "${scratch}"' EXIT

failures=0

pass() { echo "ok: $1"; }

fail() {
    echo "FAILED: $1" >&2
    failures=$((failures + 1))
}

# expect_status <status> <description> <command...>: runs the command and
# checks how it exited.
expect_status() {
    local expected="$1" description="$2"
    shift 2

    local status=0
    "$@" >"${scratch}/stdout" 2>"${scratch}/stderr" || status=$?

    if [ "${status}" -eq "${expected}" ]; then
        pass "${description}"
    else
        fail "${description}: exited ${status}, expected ${expected}"
        sed 's/^/    /' "${scratch}/stderr" >&2
    fi
}

cd "${scratch}"

# The version line names the release and the LLVM it was built with.
version="$("${tool}" --version)"
echo "${version}"

if [ -n "${expected_version}" ] && [[ "${version}" != "ghul-llvm ${expected_version} "* ]]; then
    fail "reports version '${version}', expected ${expected_version}"
else
    pass "reports its version"
fi

# A program that calls into the C library builds, runs and prints.
expect_status 0 "links a program against the C library" \
    "${tool}" "${tests}/hello.ll" -o hello

if [ "$(./hello)" = "hello from ghul-llvm" ]; then
    pass "the program runs and prints"
else
    fail "the program printed '$(./hello || true)'"
fi

# 128-bit division is a call into the compiler-rt builtins.
expect_status 0 "links a program that needs the builtins" \
    "${tool}" "${tests}/builtins.ll" -o builtins

expect_status 42 "the builtins compute the right answer" ./builtins

# What the tool builds is statically linked: no interpreter, no dynamic
# section, nothing for the system to supply at run time.
if readelf -l hello | grep -q INTERP || readelf -d hello | grep -q NEEDED; then
    fail "the program is dynamically linked"
    readelf -d hello >&2 || true
else
    pass "the program is statically linked"
fi

# Every optimisation level builds the same program.
for level in -O0 -O1 -O3; do
    expect_status 0 "builds at ${level}" "${tool}" "${level}" "${tests}/hello.ll" -o "hello${level}"
    expect_status 0 "the ${level} program runs" "./hello${level}"
done

# The same input gives the same executable, byte for byte, whatever the
# temporary object happened to be called.
"${tool}" "${tests}/hello.ll" -o first
"${tool}" "${tests}/hello.ll" -o second

if cmp -s first second; then
    pass "builds are deterministic"
else
    fail "two builds of the same input differ"
fi

# Each --emit kind stops at its own point.
expect_status 0 "emits textual IR" "${tool}" --emit=ll "${tests}/hello.ll" -o hello.opt.ll
grep -q 'define' hello.opt.ll && pass "the IR is text" || fail "the IR output is not text IR"

expect_status 0 "emits bitcode" "${tool}" --emit=bc "${tests}/hello.ll" -o hello.bc
expect_status 0 "reads bitcode back" "${tool}" hello.bc -o from-bitcode
expect_status 0 "the program built from bitcode runs" ./from-bitcode

expect_status 0 "emits assembly" "${tool}" --emit=asm "${tests}/hello.ll" -o hello.s
grep -q 'main:' hello.s && pass "the assembly defines main" || fail "the assembly has no main"

expect_status 0 "emits assembly for the default CPU" "${tool}" --emit=asm "${tests}/shift.ll" -o shift.s
grep -q shlx shift.s && pass "code is generated for x86-64-v3 by default" || fail "shift.s does not use x86-64-v3's shlx"
expect_status 0 "emits assembly for a named CPU" "${tool}" --emit=asm --cpu=x86-64 "${tests}/shift.ll" -o shift-generic.s
grep -q shlx shift-generic.s && fail "--cpu=x86-64 still used shlx" || pass "--cpu names the CPU code is generated for"

expect_status 0 "emits an object" "${tool}" --emit=obj "${tests}/hello.ll" -o hello.o
file hello.o | grep -q 'relocatable' && pass "the object is relocatable" || fail "hello.o is not an object"

# Code built separately reaches a program as an object named on the command
# line, or as an archive found through -L and -l, as a library will.
expect_status 0 "builds a separate object" "${tool}" --emit=obj "${tests}/answer.ll" -o answer.o
expect_status 0 "links a separate object" "${tool}" "${tests}/caller.ll" answer.o -o with-object
expect_status 42 "the program calls into the object" ./with-object

mkdir -p libs
ar rcs libs/libanswer.a answer.o
expect_status 0 "links an archive found with -L and -l" \
    "${tool}" "${tests}/caller.ll" -L libs -lanswer -o with-archive
expect_status 42 "the program calls into the archive" ./with-archive

expect_status 1 "reports a library it cannot find" \
    "${tool}" "${tests}/caller.ll" -lmissing -o nothing

# --keep leaves the intermediate files beside the executable.
expect_status 0 "keeps intermediate files" "${tool}" --keep "${tests}/hello.ll" -o kept

if [ -f kept.bc ] && [ -f kept.o ]; then
    pass "kept the bitcode and the object"
else
    fail "--keep did not leave kept.bc and kept.o"
fi

# A pass pipeline can be named.
expect_status 0 "runs a named pass pipeline" \
    "${tool}" '--passes=function(mem2reg,instcombine)' "${tests}/hello.ll" -o piped

expect_status 1 "refuses a pipeline it cannot parse" \
    "${tool}" '--passes=no-such-pass' "${tests}/hello.ll" -o nothing

# Bad input is a failure with LLVM's own message, not a crash.
expect_status 1 "refuses invalid IR" "${tool}" "${tests}/invalid.ll" -o invalid

if grep -q "error:" "${scratch}/stderr"; then
    pass "says why the IR is invalid"
else
    fail "no error message for invalid IR"
fi

expect_status 1 "reports a missing input" "${tool}" no-such-file.ll -o nothing
expect_status 1 "reports a missing C runtime" \
    "${tool}" --sysroot=/no/such/dir "${tests}/hello.ll" -o nothing
expect_status 2 "rejects an unknown option" "${tool}" --no-such-option "${tests}/hello.ll" -o x
expect_status 2 "insists on an output" "${tool}" "${tests}/hello.ll"

# libghul: programs that define $ghul_entry rather than main, with
# tests/libghul/support.ll standing in for the rest of a compiled program.
libghul_tests="${tests}/libghul"

expect_status 0 "builds the libghul tests' support object" \
    "${tool}" --emit=obj "${libghul_tests}/support.ll" -o support.o

for program in output process process-path spawn spawn-missing lines characters collector scalars unhandled exit exceptions escaped bigint bigint-churn bigint-negative-exponent files; do
    expect_status 0 "links ${program} with libghul" \
        "${tool}" "${libghul_tests}/${program}.ll" support.o -o "${program}"
done

# check <description> <expected> <actual>
check() {
    if [ "$2" = "$3" ]; then
        pass "$1"
    else
        fail "$1: expected '$2', got '$3'"
    fi
}

status=0
./output >output.out 2>output.err || status=$?
check "the entry's result is the exit status" 3 "${status}"
check "standard output is UTF-8, with an unpaired surrogate replaced" \
    "$(printf 'stdout  again\303\251\342\202\254\360\237\230\200\357\277\275\n\360\237\230\200\n' | od -An -c)" \
    "$(od -An -c output.out)"
check "standard error is written" "stderr" "$(cat output.err)"

check "arguments, environment names and a lookup reach the program" \
    "$(printf 'na\303\257ve \342\230\203\n\ntwo\nA\nGHUL_TEST\nh\303\251llo')" \
    "$(env -i A=b GHUL_TEST=héllo ./process "naïve ☃" "" two)"
check "a variable that is not set looks up as absent" "unset" "$(env -i ./process)"

status=0
spawned="$(./spawn)" || status=$?
check "a spawned command's output and error are captured" "$(printf 'out\nerr')" "${spawned}"
check "a spawned command's status is answered" 4 "${status}"

status=0
./spawn-missing || status=$?
check "a program that is not there is not found" 1 "${status}"

check "the process path is the executable, and the wall clock is set" \
    "$(printf '%s\nnow' "$(readlink -f ./process-path)")" \
    "$(./process-path)"

check "lines end at \\n, \\r\\n and \\r, and at the end of the input" \
    "$(printf '>one\n>two\n>three\n>last')" \
    "$(printf 'one\r\ntwo\rthree\nlast' | ./lines)"
check "an empty input has no lines" "" "$(./lines </dev/null)"

status=0
rest="$(printf 'xab\342\202\254c' | ./characters)" || status=$?
check "peek and read agree, and the rest is read to the end" "0 ab€c" "${status} ${rest}"
status=0
./characters </dev/null >/dev/null || status=$?
check "an empty input reads as its end" 0 "${status}"

# Four gigabytes allocated under a one gigabyte limit: only a collector that
# reclaims garbage, and keeps what is still reachable, gets to the end.
status=0
(ulimit -v 1048576 && ./collector) || status=$?
check "the collector reclaims garbage and keeps what is reachable" 0 "${status}"

status=0
./scalars || status=$?
check "math, the clock, random numbers and the ABI version answer as they should" 0 "${status}"

status=0
./unhandled >unhandled.out 2>unhandled.err || status=$?
check "an unhandled exception exits with 134" 134 "${status}"
check "output written before the exception is flushed" "before" "$(cat unhandled.out)"
check "the program describes the exception" \
    "Unhandled exception. Test.Failure: it broke" "$(cat unhandled.err)"

status=0
./exit >exit.out || status=$?
check "leaving through the exit host operation keeps its status" 5 "${status}"
check "leaving through the exit host operation flushes output" "written, no end." "$(cat exit.out)"

# Exceptions thrown through landing pads, unoptimised and optimised: what
# the unwinder does depends on the code it unwinds through.
expected_catches="$(printf 'caught red\nfinally (normal)\nfinally (unwinding)\ninner saw it\nouter caught blue')"

expect_status 0 "links the exception test unoptimised" \
    "${tool}" -O0 "${libghul_tests}/exceptions.ll" support.o -o exceptions-O0

for build in exceptions exceptions-O0; do
    status=0
    output="$(./${build} 2>&1)" || status=$?
    check "${build}: catch, finally and nested regions see the exception in order" \
        "0 ${expected_catches}" "${status} ${output}"
done

status=0
./escaped >escaped.out 2>escaped.err || status=$?
check "an exception that passes every landing pad is unhandled, with 134" 134 "${status}"
check "the landing pads it passed ran first" "$(printf 'catch: pass\nfinally')" "$(cat escaped.out)"
check "and it is reported" "Unhandled exception. Test.Escaped: got out" "$(cat escaped.err)"

# Every bigint operation, against what the Wasm target's loader answers for
# the same operations.
check "bigint operations answer as the Wasm target's do" \
    "$(cat "${libghul_tests}/bigint.expected")" "$(./bigint)"

status=0
./bigint-negative-exponent 2>negative-exponent.err || status=$?
check "mod_pow refuses a negative exponent, as .NET does" \
    "134 Unhandled exception. Ghul.ArgumentOutOfRangeException: The exponent must not be negative." \
    "${status} $(cat negative-exponent.err)"

# A gigabyte of bigints under half that limit: the digits are collected.
status=0
(ulimit -v 524288 && ./bigint-churn) || status=$?
check "bigint digits are collected" 0 "${status}"

# Files and directories on the real file system, in a directory of the
# test's own. Codes: 0 done, 1 no file, 2 no directory, 3 denied, 4 I/O.
mkdir files-test
expected_files="$(printf '%s\n' "${scratch}" 2 0 0 0 1 "hello world" 0 replaced null 1 null 2 2 null 3 \
    0 2 0 4 0 1 a.txt 1 p -2 4 0 0 2 \
    0 0 hello 0 4 0 0 2 1 0 0 \
    0 0 0 2 \
    0 2 0)"
check "files are written, read, listed and deleted as .NET does" "${expected_files}" "$(./files)"

check "reports the ABI version" 3 "$("${tool}" --abi-version)"

# ghul-llvm itself needs nothing from the system but the C library.
needed="$(readelf -d "${tool}" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | sort | tr '\n' ' ')"

for library in ${needed}; do
    case "${library}" in
    libc.so.6 | libm.so.6 | ld-linux-x86-64.so.2) ;;
    *) fail "ghul-llvm needs ${library}" ;;
    esac
done

pass "ghul-llvm needs only: ${needed:-nothing}"

glibc="$(objdump -T "${tool}" | grep -oE 'GLIBC_[0-9.]+' | sort -V -u | tail -n 1)"
echo "ghul-llvm needs ${glibc} or later"

if [ "${failures}" -ne 0 ]; then
    echo "${failures} check(s) failed" >&2
    exit 1
fi

echo "all checks passed"
