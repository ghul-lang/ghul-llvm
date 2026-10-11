# ghul-llvm

The tool the [ghūl](https://ghul.dev) compiler runs to turn the LLVM IR it
writes into a native executable. It is an IR assembler, optimiser, code
generator and linker in one program, built from LLVM's and LLD's libraries.
It is not a C compiler, and it runs no other program.

```sh
ghul-llvm [options] program.ll -L <runtime dir> -l<library> -o program
```

1. Reads the input, as text IR or bitcode, and verifies it. Invalid IR is
   reported with LLVM's own message, and the tool exits with status 1.
2. Optimises it with LLVM's default pipeline for the optimisation level
   (`-O2` unless told otherwise), or with a pipeline given by `--passes`.
3. Generates an object for the target.
4. Links it with LLD, statically, against the libraries it is given and the
   C runtime that ships with the tool: musl's start files and C library, and
   LLVM's compiler-rt builtins. Nothing is read from the system it runs on.

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
  lib/x86_64-unknown-linux-musl/   crt1.o crti.o crtn.o libc.a libclang_rt.builtins.a
  licenses/
```

`ghul-llvm` itself needs glibc 2.34 or later and nothing else, which covers
Ubuntu 22.04, Debian 12, RHEL 9 and their successors. x86-64 Linux is the
only target so far.

## Building

Everything is built in a container, so the only requirements are Docker and
a network connection:

```sh
build/all.sh
```

That fetches LLVM's own release build for the pinned version (prebuilt
static libraries for LLVM and LLD; nothing from LLVM is compiled here), builds
musl, builds `ghul-llvm` into `out/`, packs the release archive into `dist/`
and runs `tests/smoke.sh` against it. The first run downloads about 1.1GB;
everything is cached under `.cache/`, and a later run takes under a minute.

The pieces can be run on their own, inside the build environment:

```sh
build/docker.sh build/sysroot.sh   # musl and the builtins, into .cache/
build/docker.sh build/tool.sh      # ghul-llvm, into out/
build/docker.sh build/package.sh   # the release archive, into dist/
tests/smoke.sh                     # checks the build in out/
```

The versions of LLVM and musl, and their checksums, are pinned in
`build/versions.sh`.

## Licence

ghul-llvm is MIT licensed. The release archive also carries LLVM and LLD
(Apache 2.0 with LLVM exceptions), musl (MIT), zlib and zstd; their licences
are in its `licenses/` directory.
