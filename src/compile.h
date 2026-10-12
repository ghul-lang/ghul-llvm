#pragma once

#include "options.h"

#include "llvm/ADT/SmallVector.h"

#include <string>

namespace ghul_llvm {

// Reads and verifies the input IR, links libghul from `sysroot` into it when
// an executable is wanted, optimises it, then produces what the options
// ask for: written to the output file for IR, bitcode, assembly or an object,
// or returned in `object` when an executable is to be linked from it.
//
// Reports any failure on standard error and returns false.
bool compile(const Options &options, const std::string &sysroot,
             llvm::SmallVectorImpl<char> &object);

} // namespace ghul_llvm
