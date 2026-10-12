#pragma once

#include "options.h"

#include "llvm/ADT/StringRef.h"

#include <string>

namespace ghul_llvm {

// The directory the C runtime and libghul are read from: the one --sysroot
// names, or lib/<triple> beside the directory the executable is in.
std::string sysroot_of(const Options &options, const char *argv0);

// Links `object` with the options' objects, archives and libraries, and the
// C runtime in the sysroot, into a statically linked executable at the
// output path. `argv0` locates the default sysroot.
//
// Reports any failure on standard error and returns false.
bool link(const Options &options, llvm::StringRef object, const char *argv0);

} // namespace ghul_llvm
