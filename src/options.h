#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ghul_llvm {

// What a run is asked to produce. Each stops the pipeline at a later point
// than the one before it.
enum class Emit {
    IR,         // optimised IR, as text
    Bitcode,    // optimised IR, as bitcode
    Assembly,   // target assembly
    Object,     // an object file
    Executable, // a linked, statically linked executable
    Cell,       // a position independent shared object a host loads with dlopen
};

struct Options {
    std::string input;

    // Objects and archives named on the command line, passed to the linker
    // as they are, after the object compiled from the input.
    std::vector<std::string> link_inputs;

    std::string output;

    Emit emit = Emit::Executable;

    // 0 to 3, as -O0 to -O3.
    unsigned optimisation = 2;

    // A new pass manager pipeline that replaces the default one.
    std::optional<std::string> passes;

    std::string cpu = GHUL_LLVM_CPU;

    // Where crt1.o, libc.a and the rest of the C runtime are. Defaults to
    // the directory beside the executable.
    std::optional<std::string> sysroot;

    std::vector<std::string> library_paths;
    std::vector<std::string> libraries;

    // Keep the intermediate files beside the output.
    bool keep = false;

    bool verbose = false;
};

// The outcome of parsing a command line: options to run with, or an exit
// status because there is nothing to run (--help, --version, or a usage
// error already reported).
struct Parsed {
    std::optional<Options> options;
    int exit_status = 0;
};

Parsed parse_options(int argc, char **argv);

} // namespace ghul_llvm
