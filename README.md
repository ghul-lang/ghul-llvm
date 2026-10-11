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
| `--emit=<kind>` | stop early: `obj`, `asm`, `bc` or `ll` rather than `exe` |
| `--keep` | keep the optimised bitcode and the object, as `<output>.bc` and `<output>.o` |
| `--cpu=<cpu>` | the CPU to generate code for; the target's baseline by default |
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

## The libghul ABI

**Version 3.** `ghul-llvm --abi-version` prints it, and a program can refer to
`$ghul_abi_3`, which libghul defines, to fail to link against a libghul of any
other ABI version. Version 3 put a `$` in front of every name on either side
of the boundary; version 2 added exceptions that can be caught (the
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

### What libghul provides

- `main`, which starts the collector, keeps the arguments and environment,
  calls `$ghul_entry` and returns its result. Standard output is flushed on
  every way out: returning, `exit`, and an unhandled exception.
- `void *$ghul_alloc(const void *descriptor, size_t size)`: `size` bytes,
  header included, from the collected heap, zeroed, with `descriptor` in the
  first word. The collector scans it for pointers.
- `void *$ghul_alloc_atomic(const void *descriptor, size_t size)`: the same,
  for an object holding no pointers (strings, arrays of numbers, boxed
  numbers), which the collector does not scan.
- `_Noreturn void $ghul_throw(void *exception)`: throws the exception,
  unwinding to the nearest landing pad (below). Where no frame has one, the
  exception is unhandled: libghul calls `$ghul_report_unhandled`, flushes
  output and exits with status 134, as a .NET process does.
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
  thrown from are still there; the process then exits with status 134.

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
| `host.random_u32` | `int32_t (void)` | 32 random bits |
| `host.math_<f>` | `double (double)` | `sqrt`, `cbrt`, `floor`, `ceiling`, `truncate`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `sinh`, `cosh`, `tanh`, `exp`, `log`, `log10`, `log2` |
| `host.math_atan2`, `host.math_pow` | `double (double, double)` | |
| `host.file_current_directory` | `void *(void)` | |
| `host.file_kind` | `int32_t (path)` | 0 nothing there, 1 a file, 2 a directory |
| `host.file_read` | `void *(path)` | the whole file as `ubyte[]`, or `NULL` with the reason in `file_last_error` |
| `host.file_last_error` | `int32_t (void)` | the code of the last `file_read` |
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

Every `host.bigint_*` operation the core library declares is provided, over
libtommath, with the semantics of .NET's `BigInteger` as the Wasm target
gives them: division truncates, a remainder takes the dividend's sign, shifts
are arithmetic and a negative count shifts the other way, the bitwise
operations act on two's complement, `bigint_to_long` keeps the low 64 bits,
`bigint_to_double` truncates to the 53 bits a double holds, and
`bigint_to_hex` writes the fewest two's complement digits that keep the sign.
`bigint_hash` is the low 32 bits of `value ^ (value >> 32)`, as on Wasm:
.NET's own hash changes from one process to the next, so nothing else can
match it. Division by zero is for
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
tests/smoke.sh                     # checks the build in out/
```

The versions of LLVM, musl, the collector and libtommath, and their checksums,
are pinned in `build/versions.sh`. `GHUL_LLVM_CACHE` moves the cache, so several
checkouts can share one copy of LLVM's release.

## Licence

ghul-llvm and libghul are MIT licensed. The release archive also carries
LLVM and LLD (Apache 2.0 with LLVM exceptions), musl (MIT), the Boehm
collector (MIT-style), libtommath (public domain), zlib and zstd; their licences are in its `licenses/`
directory. Of those, only musl, the collector, libunwind, libtommath and the compiler-rt
builtins are linked into the programs ghul-llvm builds.
