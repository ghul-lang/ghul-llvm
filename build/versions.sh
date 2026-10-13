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

# LLVM's source release, of which only libunwind is built here: the unwinder
# ghūl exceptions are thrown through, linked into every executable.
LLVM_SOURCE_SHA256=c44186a7762ed28954be72e5ff6df9808e0779d4f1bf014ecc4e7e211d31ee34
LLVM_SOURCE_URL=https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/llvm-project-${LLVM_VERSION}.src.tar.xz

# The builtins in that release that are linked into every executable.
LLVM_BUILTINS=lib/clang/${LLVM_VERSION%%.*}/lib/x86_64-unknown-linux-gnu/libclang_rt.builtins.a

# The C library executables are linked against, built from source: it is
# small, and building it is what makes the version a pin rather than whatever
# a distribution currently packages.
MUSL_VERSION=1.2.6
MUSL_SHA256=d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a
MUSL_URL=https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz

# The arbitrary-precision arithmetic libghul's bigint operations use, built
# against musl with its digits allocated from the collector, and shipped as
# libtommath.a.
LIBTOMMATH_VERSION=1.3.0
LIBTOMMATH_SHA256=296272d93435991308eb73607600c034b558807a07e829e751142e65ccfa9d08
LIBTOMMATH_URL=https://github.com/libtom/libtommath/releases/download/v${LIBTOMMATH_VERSION}/ltm-${LIBTOMMATH_VERSION}.tar.xz

# The garbage collector libghul allocates from, built single-threaded
# against musl and shipped as libgc.a.
BDWGC_VERSION=8.2.12
BDWGC_SHA256=42e5194ad06ab6ffb806c83eb99c03462b495d979cda782f3c72c08af833cd4e
BDWGC_URL=https://github.com/ivmai/bdwgc/releases/download/v${BDWGC_VERSION}/gc-${BDWGC_VERSION}.tar.gz

# The stack walker's symbolizer: names each frame of a backtrace from the
# executable's own symbol table, and gives its file and line where the
# program was built with debug information. Pinned by commit, since the
# project makes no releases.
LIBBACKTRACE_COMMIT=0b9b49cf4a2c9229fc052d6716e1528b2f23e91a
LIBBACKTRACE_SHA256=fabcb6868b3d09734d4c537637dacac0bf8c9aa28e07486de5c6ce2eb749d413
LIBBACKTRACE_URL=https://github.com/ianlancetaylor/libbacktrace/archive/${LIBBACKTRACE_COMMIT}.tar.gz
