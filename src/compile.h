#pragma once

#include "options.h"

#include "llvm/ADT/SmallVector.h"

namespace ghul_llvm {

// Reads, verifies and optimises the input IR, then produces what the options
// ask for: written to the output file for IR, bitcode, assembly or an object,
// or returned in `object` when an executable is to be linked from it.
//
// Reports any failure on standard error and returns false.
bool compile(const Options &options, llvm::SmallVectorImpl<char> &object);

} // namespace ghul_llvm
