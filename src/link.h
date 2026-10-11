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

} // namespace ghul_llvm
