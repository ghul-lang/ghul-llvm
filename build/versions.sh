# Sourced by the build scripts. Everything ghul-llvm is built from is pinned
# here, by version and by checksum, so a release can be rebuilt from the same
# inputs. Changing any of these changes the CI cache key.

# The target triple the executables this build produces run on. One release
# archive is built per triple, and it is also the platform the tool itself
# runs on.
TRIPLE=x86_64-unknown-linux-musl

# LLVM's own release build: prebuilt static libraries for LLVM and LLD, and
# the compiler-rt builtins. Nothing from LLVM is compiled here.
LLVM_VERSION=23.1.3
LLVM_RELEASE=LLVM-${LLVM_VERSION}-Linux-X64
LLVM_SHA256=14d2f701eb68fb799001bdea6231048f3990690fa2f406d563555ff8f744daba
LLVM_URL=https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${LLVM_RELEASE}.tar.zst

# The builtins in that release that are linked into every executable.
LLVM_BUILTINS=lib/clang/${LLVM_VERSION%%.*}/lib/x86_64-unknown-linux-gnu/libclang_rt.builtins.a

# The C library executables are linked against, built from source: it is
# small, and building it is what makes the version a pin rather than whatever
# a distribution currently packages.
MUSL_VERSION=1.2.6
MUSL_SHA256=d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a
MUSL_URL=https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz
