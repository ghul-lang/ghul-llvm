# AGENTS.md

Guidance for AI agents working in this repository.

## What this is

`ghul-llvm`, the one external program the ghūl compiler's LLVM target runs:
LLVM IR in, statically linked executable out. A few hundred lines of C++ over
LLVM's and LLD's libraries. See README.md for what it does and how it is
used, and `.github/claude-review.md` for what to be careful of.

## Layout

- `src/` — the tool. `options.cpp` parses the command line; `compile.cpp`
  reads, verifies, optimises and generates code; `link.cpp` drives LLD as a
  library; `main.cpp` ties them together. `polly.cpp` is a stub explained in
  its own comment.
- `libghul/` — libghul (`libghul.a`), ghūl's native support library: C,
  built with musl-gcc.
  `libghul/include/ghul.h` declares the ABI that README.md describes.
- `CMakeLists.txt` — builds `src/` against an unpacked LLVM release, and
  generates the stubs that keep every backend but the target's out of the
  executable.
- `build/` — the build, all of it run inside the container `build/Dockerfile`
  describes, through `build/docker.sh`. `versions.sh` pins every input.
  `all.sh` runs everything.
- `tests/` — `smoke.sh` and the IR it builds. `tests/libghul/` holds programs
  that exercise libghul, with `support.ll` standing in for what a
  compiled program provides.
- `licenses/` — licences of third-party code shipped in the release archive
  that the build cannot copy from elsewhere.

## Building and testing

```sh
build/all.sh          # everything, from a clean checkout; about a minute once cached
tests/smoke.sh        # checks the build in out/
```

The smoke test runs on the host, not in the container: the point of the
tool is that it runs on an ordinary Linux, and the container would hide a
dependency on something only it has.

## Things that are easy to get wrong

- **Linking a second backend.** LLVM's release libraries contain every
  backend. Anything that references one, directly or through
  `InitializeAll*`, pulls it into the executable and roughly doubles its
  size. CMake generates no-op initialisers for every backend but the
  target's; `main.cpp` initialises the target's by name. Check the size of
  `out/bin/ghul-llvm` after a change that touches initialisation or LLVM's
  version.
- **Reading files from the host.** The link must use only the sysroot and
  the `-L` directories. LLD searches no default paths unless told to, so
  never pass it `--sysroot`, a `-L` it was not given, or a library by bare
  name.
- **Changing the ABI.** Anything in `ghul.h` that compiled code
  depends on is the contract with the compiler's LLVM backend. A change to it
  raises `GHUL_ABI_VERSION`, renames `$ghul_abi_<n>` to match,
  updates the README's ABI section, and sets `requirements.json`'s `abi`
  and its `minimum-compiler` to the first compiler release that emits the
  new ABI, in the same change.
- **Unwind tables.** An exception unwinds through libghul's frames as
  well as the program's, so libghul is compiled with `-funwind-tables`
  and nothing in it may be built without them. `tests/libghul/exceptions.ll`
  is built at both `-O0` and `-O2`, because what the unwinder sees depends on
  the code it unwinds through.
- **bigint answers.** `tests/libghul/bigint.expected` holds what the Wasm
  target's loader answers for the operations `bigint.ll` runs, computed with
  JavaScript's `BigInt` using the loader's own definitions in
  `ghul/src/ir/wasmgc/loader.mjs`. A difference is a bug in libghul, unless
  the loader changed first.
- **Raising the glibc requirement.** The build runs on Ubuntu 22.04 so that
  `ghul-llvm` needs no newer glibc than LLVM's release does. Building on a
  newer distribution raises it silently; the smoke test prints the version
  required.
