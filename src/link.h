#pragma once

#include "options.h"

#include "llvm/ADT/StringRef.h"

namespace ghul_llvm {

// Links `object` with the options' objects, archives and libraries, and the
// C runtime in the sysroot, into a statically linked executable at the
// output path. `argv0` locates the default sysroot.
//
// Reports any failure on standard error and returns false.
bool link(const Options &options, llvm::StringRef object, const char *argv0);

// Links `object` with the options' objects, archives and libraries into an
// executable at the output path that is linked dynamically against the
// system's glibc, with the C runtime in the gnu sysroot. Libraries named
// with -l are the system's shared libraries, found by find_library.
// `argv0` locates the default sysroot.
//
// Reports any failure on standard error and returns false.
bool link_dynamic(const Options &options, llvm::StringRef object, const char *argv0);

// Links `object` into a shared object at the output path that a glibc host
// loads with dlopen. It takes nothing from the C runtime: the names it
// leaves undefined are the host's, and are bound when it is loaded.
//
// Reports any failure on standard error and returns false.
bool link_cell(const Options &options, llvm::StringRef object);

} // namespace ghul_llvm
