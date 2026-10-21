# ghul-llvm

The tool the [ghūl](https://ghul.dev) compiler runs to turn the LLVM IR it
writes into a native executable. It is an IR assembler, optimiser, code
generator and linker in one program, built from LLVM's and LLD's libraries.
It is not a C compiler, and it runs no other program.

```sh
ghul-llvm [options] program.ll -L <library dir> -l<library> -o program
```

1. Reads the input, as text IR or bitcode, and verifies it. Invalid IR is
   reported with LLVM's own message, and the tool exits with status 1.
2. Optimises it with LLVM's default pipeline for the optimisation level
   (`-O2` unless told otherwise), or with a pipeline given by `--passes`.
3. Generates an object for the target.
4. Links it with LLD, statically, against the libraries it is given and
   what ships with the tool: libghul (`libghul.a`), ghūl's native support
   library; libtommath (`libtommath.a`) for large bigints; the Boehm garbage
   collector (`libgc.a`); LLVM's unwinder
   (`libunwind.a`); musl's start files and C library; and LLVM's compiler-rt
   builtins. Nothing is read from the system it runs on.

The executable it produces is statically linked and runs on any Linux of the
same architecture.

## Options

| option | meaning |
|---|---|
| `-o <file>` | where to write the output |
| `-O0` ... `-O3` | optimisation level; `-O2` by default |
| `--passes=<pipeline>` | a new pass manager pipeline to run instead of the default, such as `function(mem2reg,instcombine)` |
| `--emit=<kind>` | stop early: `obj`, `asm`, `bc` or `ll` rather than `exe`; or `cell`, a shared object for the cell host (below) |
| `--keep` | keep the optimised bitcode and the object, as `<output>.bc` and `<output>.o` |
| `--cpu=<cpu>` | the CPU to generate code for; `x86-64-v3` (AVX2, 2013 and later) by default |
| `-L <dir>`, `-l <name>` | where to look for libraries, and which to link (`lib<name>.a`) |
| `--sysroot=<dir>` | where the C runtime is; by default `lib/<triple>` beside `bin/` |
| `-v` | print the linker command line |
| `--abi-version` | print the version of the libghul ABI, below |
| `--version`, `--help` | |

Objects (`.o`) and archives (`.a`) named on the command line are linked as
they are. Exit status is 0 on success, 1 when the input could not be built,
and 2 for a usage error.

## Releases

Each release carries one archive per target, named for the target's triple,
so a client downloads only the one for the machine it runs on:

```
ghul-llvm-x86_64-unknown-linux-musl.tar.gz
ghul-llvm-x86_64-unknown-linux-musl.tar.gz.sha256
```

The archive unpacks to a single directory:

```
ghul-llvm-x86_64-unknown-linux-musl/
  bin/ghul-llvm
  lib/x86_64-unknown-linux-musl/   libghul.a libtommath.a libgc.a libunwind.a crt1.o crti.o crtn.o libc.a libclang_rt.builtins.a
  licenses/
```

`ghul-llvm` itself needs glibc 2.34 or later and nothing else, which covers
Ubuntu 22.04, Debian 12, RHEL 9 and their successors. x86-64 Linux is the
only target so far.

## Cells and the cell host

`--emit=cell` writes the program as a position independent shared object, a
*cell*, that a running host loads with `dlopen` instead of an executable that
is started. A cell is linked with nothing: no start files, no C library, no
libghul. Every name it leaves undefined, `$ghul_alloc`, `memcpy`, the host
operations, is bound to the host's when the cell is loaded, and the names it
defines for libghul to call, `$ghul_entry` and the rest of "What the program
provides", are looked up in the cell by the host. Its code is generated for the
same machine under the gnu environment, and its null checks stay explicit.

The host, `ghul-host`, is a glibc executable with libghul, the collector, the
unwinder and the big integer library linked into it whole and exported. It
starts the collector, loads each cell named on its command line, runs its
entry point, and exits with the last entry's status; arguments after `--` are
the program's. The collector scans a loaded cell's data, and exceptions unwind
through a cell's frames as through any other. It is released as a second
archive, `ghul-llvm-host-<arch>-unknown-linux-gnu.tar.gz`, built in Debian 12
so that glibc is its only dependency and 2.34 the oldest it runs on. Cells are
loaded with `RTLD_GLOBAL` and bound lazily, so a cell can call what an earlier
cell defines, and what a later one does, by name. A cell need not have an
entry point. Binding is lazy on purpose, so a name a later cell defines can be
called before that cell is loaded; the cost is that a call to a name no cell ever
defines ends the process, with the dynamic loader's message naming the symbol,
where an eager load would have refused the cell.

`ghul-host --serve <commands>` keeps the process, and so every cell's code and
data, for as long as it is asked to. It reads one line at a time from the file or
named pipe `<commands>`; a line naming a cell loads and runs it, and `quit` ends
the host. The program's standard input and output stay the program's, and after
each cell the host flushes what it printed and writes `\x01cell-done <status>` to
standard output, so the driver knows where that cell's output ends. An exception
nothing in the cell catches is reported and ends that cell, with status 134, not
the session: libghul takes it back to the host instead of ending the process
when the host sets `$libghul_unhandled_jump`.

A SIGINT sent to the host while a cell runs ends that cell with status 130 and
leaves the session as the cell left it; one that arrives between cells is
ignored. The cell is abandoned where it stands, so work it was part way through,
an allocation included, is not finished.

## The libghul ABI

**Version 4.** `ghul-llvm --abi-version` prints it, and a program can refer to
`$ghul_abi_4`, which libghul defines, to fail to link against a libghul of any
other ABI version. Version 4 left an object's descriptor for the program to
store, so the optimiser sees every store that initialises an object; version 3
put a `$` in front of every name on either side of the boundary; version 2 added exceptions that can be caught (the
landing-pad contract below, `$ghul_caught` and `$ghul_personality`); version 1
treated every exception as unhandled. A change to anything below that a
compiled program depends on raises it; adding an operation does not. `libghul/include/ghul.h` has the C
declarations.

**Names.** Every symbol libghul defines, and every symbol it expects the
program to export, starts with `$`: `$ghul_alloc`, `$ghul_host_write`,
`$ghul_entry` and the rest. `$` is an operator character in ghūl, so no ghūl
identifier can spell one of these names, and a program's own functions keep
their plain names. LLVM's text IR takes them unquoted, as `@$ghul_alloc`.
Generated code calls nothing outside the program but `$ghul_*` functions and
LLVM's own `llvm.*` intrinsics: anything it needs from the C library is a
`$ghul_` function in libghul.

libghul is ghūl's native support library: entry, allocation, throwing and the
host operations. It is not the standard library (`ghul-runtime`), which is
ghūl code compiled into the program. It is the native counterpart of the Wasm
target's JavaScript loader. It knows nothing of how the program lays out its objects or strings: a string
crosses the boundary as UTF-16 code units, and the program builds its own
string objects when libghul asks.

### What the program provides

| symbol | |
|---|---|
| `int32_t $ghul_entry(void)` | the program's entry; its result is the exit status. An entry that takes `string[]` is wrapped by one that builds the array through `$ghul_host_argument` |
| `void *$ghul_make_string(const uint16_t *chars, int32_t length)` | builds one of the program's strings; the code units are valid only during the call |
| `void *$ghul_make_bytes(const uint8_t *bytes, int32_t length)` | builds one of the program's `ubyte[]` arrays; the bytes are valid only during the call |
| `void $ghul_report_unhandled(void *exception)` | writes `Unhandled exception. <Type>: <message>` and a newline to stream 2 through `$ghul_host_write`; needed by a program that throws |
| `void *$ghul_make_null_reference(void)` | the exception a read through null throws; a program that does not define it ends with SIGSEGV on one, as without libghul |

### What libghul provides

- `main`, which starts the collector, keeps the arguments and environment,
  calls `$ghul_entry` and returns its result. Standard output is flushed on
  every way out: returning, `exit`, and an unhandled exception.
- `void *$ghul_alloc(size_t size)`: `size` bytes, header included, from the
  collected heap, zeroed. The collector scans it for pointers. The program
  stores the object's descriptor in its first word.
- `void *$ghul_alloc_atomic(size_t size)`: the same, for an object holding no
  pointers (strings, arrays of numbers, boxed numbers), which the collector
  does not scan, and not zeroed: the program stores everything it holds.
- `void *$ghul_clone(const void *object)`: a shallow copy of the whole
  allocation, descriptor included, allocated as the same kind as the
  original, so a copy of an object the collector does not scan is not
  scanned either.
- `int32_t $ghul_identity_hash(const void *object)`: a hash of the object's
  identity, unchanged while it lives, since the collector never moves it.
- `void $ghul_register_finalizer(void *object, void (*finalizer)(void *))`:
  has the collector call `finalizer` with `object` once the object is
  unreachable. Everything the object refers to is still in memory when it
  runs, and the finalizers of finalizable objects it refers to run after it;
  finalizable objects in a cycle are never finalized. A finalizer runs inside
  the allocation that started the collection, so it must not throw. Nothing finalizes an object still reachable when the process ends,
  as on .NET.
- `_Noreturn void $ghul_throw(void *exception)`: throws the exception,
  unwinding to the nearest landing pad (below). Where no frame has one, the
  exception is unhandled: libghul calls `$ghul_report_unhandled`, flushes
  output and exits with status 134, as a .NET process does.
- `_Noreturn void $ghul_throw_null_reference(void)`: throws what
  `$ghul_make_null_reference` makes, for a test for null that found one
  (below).
- `void *$ghul_caught(void *header)`: the exception a landing pad was entered
  for.
- `$ghul_personality`: the personality routine, which only the unwinder
  calls.
- One `ghul_host_<name>` function for each operation the core library declares
  with `@intrinsic("host.<name>")`.

### Exceptions and landing pads

Exceptions are zero-cost: nothing runs on the normal path, and a throw
unwinds through LLVM's libunwind using the unwind tables LLVM emits.
libghul decides only *where* an exception stops, at the nearest landing pad;
generated code decides whether that landing pad handles it.

- **Personality.** Every function containing a `landingpad` names
  libghul's personality routine: `define ... personality ptr @$ghul_personality`.
  A function without a landing pad needs none, and must not be `nounwind` if
  an exception can pass through it.
- **Calls in a protected region** are `invoke`, with the unwind edge going to
  the region's landing pad. A call outside every protected region is a plain
  `call`, and an exception passes through the frame.
- **Landing pads.** `landingpad { ptr, i32 } catch ptr null` for a catch, and
  `landingpad { ptr, i32 } cleanup` for a finally or a disposal. libghul
  treats the two alike, entering any landing pad for any exception. The
  second value, the selector, carries nothing and is not read.
- **The exception.** The first value is the unwinder's header;
  `call ptr @$ghul_caught(ptr %header)` answers the exception it carries.
- **Not handling it.** A landing pad that does not handle the exception (no
  catch type matched, or a finally that has run) throws it on by calling
  `$ghul_throw` with the same exception: with `invoke` to an enclosing landing
  pad in the same function, or with `call` to leave the frame. `resume` is
  not used: an exception that `resume` carries past the last frame is ended
  by the unwinder with `abort`, before libghul can report it.
- **Unhandled.** An exception with no landing pad left is reported through
  `$ghul_report_unhandled` from inside `$ghul_throw`, so the frames it was
  thrown from are still there. libghul then writes those frames, as
  described under "Crash reports", and the process exits with status 134.

```llvm
define void @f() personality ptr @$ghul_personality {
entry:
  invoke void @g() to label %done unwind label %catch
done:
  ret void
catch:
  %landed = landingpad { ptr, i32 } catch ptr null
  %header = extractvalue { ptr, i32 } %landed, 0
  %exception = call ptr @$ghul_caught(ptr %header)
  ; test %exception's type; on a match, handle it and carry on, otherwise:
  call void @$ghul_throw(ptr %exception)
  unreachable
}
```

### Reading through null

A read through a null reference throws the exception
`$ghul_make_null_reference` makes, where the program defines it.

- **Tested reads.** Generated code tests the reference and branches to a
  block that calls `$ghul_throw_null_reference`, one call so the test adds
  little code, marking the branch so LLVM can fold the test into the read:
  `br i1 %is_null, label %throw, label %read, !make.implicit !{}`, with the
  read first in `%read`. At `-O1` and above the test costs nothing: the read
  is left to fault, and the fault map LLVM writes records `%throw` as where it
  resumes. A throw from `%throw` reaches a landing pad in the same function
  as any other. A test LLVM cannot fold stays a compare and a branch.
- **Untested reads.** A fault in the first 64KB at any other instruction
  throws as though that instruction had called `$ghul_throw_null_reference`: the exception
  passes out of the faulting function to its callers. LLVM takes a read
  through null to be undefined, so whether it still happens after
  optimisation is not guaranteed; generated code tests every read through a
  reference that can be null.
- **Anything else.** A fault further from null, or in a program that neither
  defines `$ghul_make_null_reference` nor calls `$ghul_throw_null_reference`, is
  reported as described under "Crash reports" and then goes to the handler
  installed before libghul's, which ends the process with SIGSEGV.

### Crash reports

An unhandled exception, a fault that is not a read through null, a bus
error, an arithmetic fault and an illegal instruction are reported on
stream 2 after standard output is flushed. A fault is described in one line,
`Stack overflow.` where the stack ran out and `Fatal error. <SIGNAL>: <what>`
otherwise; an exception by `$ghul_report_unhandled`. Then the frames follow,
innermost first, one `   at <name>` line each, with ` in <file>:line <n>`
where the program carries debug information. Frames inlined into another
appear only with debug information. libghul's own frames, those outside
`main`, and `$ghul_entry` where another frame is inside it are left out, and
past the hundredth frame the rest are counted.

The walk is libunwind's, from the frames still live when the report is made,
seeded from the interrupted registers for a signal; the names come from the
executable's own symbol table and debug information, read by libbacktrace.
Nothing of it runs before a report. The handler runs on a stack of its own,
so a stack that runs out is reported too, and ghul-llvm gives every function
an unwind table so that the walk can pass through any frame. A fault ends
the process with its signal, as it would without libghul.

### Host operations

A host operation's C signature follows from its declaration:

| ghūl | C |
|---|---|
| `int` | `int32_t` |
| `long` | `int64_t` |
| `double` | `double` |
| `char` | `uint16_t` |
| `bool` | `bool` |
| `string` argument | two parameters: `const uint16_t *chars, int32_t length` |
| `string` result | `void *`, made by `$ghul_make_string` |
| `string?` result | the same, or `NULL` when absent |
| `ubyte[]` argument | two parameters: `const uint8_t *bytes, int32_t length` |
| `ubyte[]` result | `void *`, made by `$ghul_make_bytes`, or `NULL` when absent |
| `bigint` argument or result | `void *`: a handle libghul makes and never changes, in the collected heap; a handle the program keeps must be in an object the collector scans |

| operation | C signature | notes |
|---|---|---|
| `host.write` | `void (const uint16_t *, int32_t, int32_t stream)` | UTF-8 to standard output (stream 1) or standard error (2); any other stream is standard output. Output is buffered and flushed before standard error is written and before input is read |
| `host.argument_count` | `int32_t (void)` | not counting the program's name |
| `host.argument` | `void *(int32_t)` | |
| `host.environment_count` | `int32_t (void)` | the environment as it was at start |
| `host.environment_name` | `void *(int32_t)` | |
| `host.environment_lookup` | `void *(const uint16_t *, int32_t)` | `NULL` when not set |
| `host.stdin_read` | `int32_t (void)` | a UTF-16 code unit, or -1 at the end |
| `host.stdin_peek` | `int32_t (void)` | the same, without taking it |
| `host.stdin_read_line` | `void *(void)` | without its ending (`\n`, `\r\n` or `\r`); `NULL` at the end |
| `host.stdin_read_to_end` | `void *(void)` | everything left, possibly empty |
| `host.monotonic_milliseconds` | `double (void)` | since the program started |
| `host.sleep` | `void (int32_t)` | waits for at least that many milliseconds; zero or less returns at once |
| `host.is_terminal` | `bool (int32_t)` | whether standard stream 0, 1 or 2 is a terminal |
| `host.process_begin` | `void (file_name)` | starts a command; a name with no `/` is found on the `PATH` |
| `host.working_set_bytes` | `double (void)` | the resident set, read from `/proc/self/statm`; 0 where it cannot be read |
| `host.process_add_argument` | `void (argument)` | adds the next argument |
| `host.process_run` | `int32_t (void)` | runs the command to completion, standard input shared, standard output and error captured; the exit status, 128 plus the signal for one that was killed, or -1 where it could not start |
| `host.process_failure` | `int32_t (void)` | why the last run could not start: 0 it did, 1 the program was not found, 2 anything else |
| `host.process_output`, `host.process_error` | `void *(void)` | what the last run wrote to standard output and standard error |
| `host.wall_clock_milliseconds` | `double (void)` | since 1970-01-01T00:00:00Z |
| `host.exit` | `void (int32_t)` | ends the process with that status, after flushing standard output |
| `host.process_path` | `void *(void)` | the running executable, from `/proc/self/exe`; `NULL` where it cannot be read |
| `host.random_u32` | `int32_t (void)` | 32 random bits |
| `host.math_<f>` | `double (double)` | `sqrt`, `cbrt`, `floor`, `ceiling`, `truncate`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `sinh`, `cosh`, `tanh`, `exp`, `log`, `log10`, `log2` |
| `host.math_atan2`, `host.math_pow` | `double (double, double)` | |
| `host.file_current_directory` | `void *(void)` | |
| `host.file_kind` | `int32_t (path)` | 0 nothing there, 1 a file, 2 a directory |
| `host.file_read` | `void *(path)` | the whole file as `ubyte[]`, or `NULL` with the reason in `file_last_error` |
| `host.file_last_error` | `int32_t (void)` | the code of the last failed `file_read`, `file_open_read` or `file_read_into` |
| `host.file_open_read` | `int32_t (path)` | a descriptor reading the file as it is asked for, or -1 with the reason in `file_last_error` |
| `host.file_read_into` | `int32_t (int32_t descriptor, bytes, int32_t offset, int32_t count)` | reads at most `count` bytes into `bytes` from `offset`: how many, 0 at the end, or -1 |
| `host.file_close` | `int32_t (int32_t descriptor)` | |
| `host.file_write` | `int32_t (path, bytes, bool append)` | replaces the content, or adds to its end |
| `host.file_delete` | `int32_t (path)` | a file that is not there is not an error |
| `host.file_rename` | `int32_t (from, to)` | a file or a directory; nothing may be at `to` already |
| `host.directory_set_current` | `int32_t (path)` | the process's current directory |
| `host.directory_create` | `int32_t (path)` | with every directory on the way |
| `host.directory_delete` | `int32_t (path, bool recursive)` | not empty and not recursive, or not there, is an error |
| `host.directory_entries` | `int32_t (path, bool directories)` | the number of files, or directories, directly in it, or a code negated |
| `host.directory_entry` | `void *(int32_t)` | each name from the last listing, sorted |

Input is decoded from UTF-8 and output encoded to it; a byte sequence that is
not UTF-8, or an unpaired surrogate, becomes U+FFFD.

Every `host.bigint_*` operation the core library declares is provided,
over libtommath, with the semantics of .NET's `BigInteger` as the Wasm
target gives them: division truncates, a remainder takes the dividend's
sign, shifts are arithmetic and a negative count shifts the other way,
the bitwise operations act on two's complement, `bigint_to_long` keeps
the low 64 bits, `bigint_to_double` truncates to the 53 bits a double
holds, and `bigint_to_hex` writes the fewest two's complement digits that
keep the sign. `bigint_hash` is the low 32 bits of
`value ^ (value >> 32)`, as on Wasm: .NET's own hash changes from one
process to the next, so nothing else can match it. Division by zero is for
the core library to check before calling; an argument an operation cannot
take, such as a double that is not finite or a negative exponent, ends the
process as an unhandled exception.

Files are the real file system, as on .NET. A path argument is a `string`,
which the core library makes full first. The file operations answer a code:
0 done, 1 the file is not there, 2 a directory on the way is not there, 3
access is denied, 4 any other I/O error; the core library throws
`FileNotFoundException`, `DirectoryNotFoundException`,
`UnauthorizedAccessException` or `IOException` for them, as .NET does.

`host.input_file_*` and `host.file_written` belong to the Wasm target's file
system, which is held in memory, and are not provided here.

## Building

Everything is built in a container, so the only requirements are Docker and
a network connection:

```sh
build/all.sh
```

That fetches LLVM's own release build for the pinned version (prebuilt static
libraries for LLVM and LLD, which are not compiled here), builds musl, the
collector, libunwind and libtommath, builds `ghul-llvm` and libghul into
`out/`, packs the release archive into `dist/` and runs `tests/smoke.sh`
against it. The first run downloads about 1.3GB; everything is cached under
`.cache/`, and a later run takes under a minute.

The pieces can be run on their own, inside the build environment:

```sh
build/docker.sh build/sysroot.sh   # musl, the builtins and the collector, into .cache/
build/docker.sh build/tool.sh      # ghul-llvm, into out/
build/docker.sh build/libghul.sh   # libghul.a, into out/
build/docker.sh build/package.sh   # the release archive, into dist/
build/host-docker.sh               # the cell host, into out/host/, in debian:bookworm
build/host-package.sh              # its release archive, into dist/
tests/smoke.sh                     # checks the build in out/
tests/cell-smoke.sh out out/host   # runs a cell in the host
```

The versions of LLVM, musl, the collector, libtommath and libbacktrace, and their checksums,
are pinned in `build/versions.sh`. `GHUL_LLVM_CACHE` moves the cache, so several
checkouts can share one copy of LLVM's release.

## Licence

ghul-llvm and libghul are MIT licensed. The release archive also carries
LLVM and LLD (Apache 2.0 with LLVM exceptions), musl (MIT), the Boehm
collector (MIT-style), libtommath (public domain), libbacktrace (BSD), zlib and zstd; their licences are in its `licenses/`
directory. Of those, only musl, the collector, libunwind, libtommath, libbacktrace and the compiler-rt
builtins are linked into the programs ghul-llvm builds.
