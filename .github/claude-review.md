# Cloud code review brief

What this repository is, and what to watch for in it. Everything else — what PR
context is available, how to post a review, what makes a finding worth raising,
comment hygiene, PR-description shape, the versioning mechanism — comes from the
review workflow's runtime notes. Don't restate it here: this file is read first,
so a stale copy would silently override the current text.

Not loaded by local Claude Code; only the cloud reviewer reads this.

## What this repo is

`ghul-llvm` is the program the ghūl compiler runs for its LLVM target. It
takes the textual LLVM IR the compiler writes, verifies it, optimises it with
the new pass manager, generates an object, and links that with LLD — used as
a library, in the same process — statically against musl and compiler-rt
builtins that ship beside it. The result is a statically linked executable.

The repository also holds libghul (`libghul/`), C linked into every
program: `main`, allocation from the Boehm collector, throwing and catching
through LLVM's libunwind with a personality routine of its own, and the host
operations the core library declares. Its interface to compiled code is the
versioned ABI in `libghul/include/ghul.h` and README.md.

The tool is C++ over LLVM's own prebuilt release libraries: nothing from LLVM is
compiled here. The release is one archive per target triple, holding the tool
and the C runtime. Only `x86_64-unknown-linux-musl` exists so far.

## What to watch for here

- **The executable's size.** LLVM's release libraries carry every backend.
  `CMakeLists.txt` generates no-op initialisers for all but the target's, and
  `main.cpp` initialises the target's backend by name. A change that calls
  any `InitializeAll*` function, or references another backend, links that
  backend in and grows the tool by tens of megabytes without failing anything.
- **Host files.** Linking must read only the sysroot, the inputs named on the
  command line and the `-L` directories. A default search path, or a library
  named without a directory, would make the output depend on the machine it
  was built on.
- **Determinism.** The same input must produce the same executable. Anything
  that puts a temporary path, a time or a random value into the output breaks
  that; the smoke test compares two builds.
- **Exit statuses.** 0 for success, 1 for input that could not be built, 2
  for a usage error. The compiler distinguishes them, so a change that
  reports a bad input as a usage error, or crashes instead of reporting, is a
  bug.
- **The ABI.** A change to what `ghul.h` declares that compiled
  code depends on, without raising `GHUL_ABI_VERSION`, renaming
  `ghul_abi_<n>` and updating the README's ABI section, is a finding.
- **libghul is ignorant of object layout.** It handles strings only as
  UTF-16 code units and builds them through `ghul_make_string`; a change that
  reads or writes the program's own objects is a finding.
- **Unwinding.** An exception crosses every frame between the throw and its
  landing pad, libghul's own included, so its code must keep its
  unwind tables (`build/libghul.sh` compiles with `-funwind-tables`). The
  personality routine reports a handler wherever a landing pad covers the
  call; a change that makes it skip landing pads, or that has generated code
  continue unwinding with `resume` rather than `ghul_throw`, loses the
  report of an unhandled exception.
- **bigint semantics.** The `host.bigint_*` operations answer as the Wasm
  target's loader does, which `tests/libghul/bigint.expected` records. A
  change to an answer there is a change of semantics between targets, not a
  test update.
- **The glibc requirement.** The build runs on Ubuntu 22.04 deliberately; a
  change to `build/Dockerfile` that moves to a newer distribution raises the
  glibc version the tool needs.
- **Pinned inputs.** Every download is pinned in `build/versions.sh` by
  version and checksum. A change that fetches anything not pinned there is a
  finding.
