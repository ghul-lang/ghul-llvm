#include "options.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <string_view>

#ifndef GHUL_LLVM_VERSION
#define GHUL_LLVM_VERSION "0.0.0-local"
#endif

#ifndef GHUL_ABI_VERSION
#error "GHUL_ABI_VERSION must be defined by the build"
#endif

#define GHUL_STRINGIFY_(x) #x
#define GHUL_STRINGIFY(x) GHUL_STRINGIFY_(x)

#ifndef GHUL_LLVM_TRIPLE
#error "GHUL_LLVM_TRIPLE must be defined by the build"
#endif

namespace ghul_llvm {

namespace {

constexpr const char *usage_text =
    "usage: ghul-llvm [options] <input.ll|input.bc> [<object or archive>...] -o <output>\n"
    "\n"
    "Assembles, optimises and compiles LLVM IR, and links the result with the\n"
    "ghul runtime, the collector and the C runtime into a statically linked\n"
    "executable for " GHUL_LLVM_TRIPLE ".\n"
    "\n"
    "options:\n"
    "  -o <file>             write the output to <file>\n"
    "  -O0, -O1, -O2, -O3    optimisation level (default -O2)\n"
    "  --passes=<pipeline>   run this new pass manager pipeline in place of\n"
    "                        the default one for the optimisation level\n"
    "  --emit=<kind>         stop after producing <kind>: exe (the default),\n"
    "                        obj, asm, bc or ll\n"
    "  --keep                keep the optimised bitcode and the object beside\n"
    "                        the executable, as <output>.bc and <output>.o\n"
    "  --cpu=<cpu>           the CPU to generate code for (default: the\n"
    "                        target's baseline)\n"
    "  -L <dir>, -L<dir>     add <dir> to the library search path\n"
    "  -l <name>, -l<name>   link against lib<name>.a\n"
    "  --sysroot=<dir>       where the C runtime is (default: lib/" GHUL_LLVM_TRIPLE "\n"
    "                        beside the directory ghul-llvm is in)\n"
    "  -v, --verbose         print the linker command line\n"
    "  --version             print the version and exit\n"
    "  --abi-version         print the version of the libghul ABI and exit\n"
    "  --help                print this and exit\n";

int usage_error(const std::string &message) {
    llvm::errs() << "ghul-llvm: " << message << "\n"
                 << "run ghul-llvm --help for usage\n";
    return 2;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

std::optional<Emit> parse_emit(std::string_view kind) {
    if (kind == "exe") return Emit::Executable;
    if (kind == "obj") return Emit::Object;
    if (kind == "asm") return Emit::Assembly;
    if (kind == "bc") return Emit::Bitcode;
    if (kind == "ll") return Emit::IR;
    return std::nullopt;
}

} // namespace

Parsed parse_options(int argc, char **argv) {
    Options options;
    Parsed parsed;

    // Takes the value of an option that is either attached (-Lfoo) or the
    // next argument (-L foo). Returns false when there is no value.
    auto value_of = [&](int &i, std::string_view flag, std::string &value) {
        std::string_view arg = argv[i];

        if (arg.size() > flag.size()) {
            value = std::string(arg.substr(flag.size()));
            return true;
        }

        if (i + 1 >= argc) {
            return false;
        }

        value = argv[++i];
        return true;
    };

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        std::string value;

        if (arg == "--help" || arg == "-h") {
            llvm::outs() << usage_text;
            return parsed;
        }

        if (arg == "--version") {
            llvm::outs() << "ghul-llvm " GHUL_LLVM_VERSION " (LLVM " LLVM_VERSION_STRING
                            ", " GHUL_LLVM_TRIPLE ", libghul ABI " GHUL_STRINGIFY(
                                GHUL_ABI_VERSION) ")\n";
            return parsed;
        }

        if (arg == "--abi-version") {
            llvm::outs() << GHUL_ABI_VERSION << "\n";
            return parsed;
        }

        if (arg == "-o") {
            if (!value_of(i, "-o", value)) {
                parsed.exit_status = usage_error("-o needs a file name");
                return parsed;
            }

            options.output = value;
        } else if (arg.size() == 3 && starts_with(arg, "-O") && arg[2] >= '0' &&
                   arg[2] <= '3') {
            options.optimisation = arg[2] - '0';
        } else if (starts_with(arg, "--passes=")) {
            options.passes = std::string(arg.substr(std::strlen("--passes=")));
        } else if (starts_with(arg, "--emit=")) {
            auto emit = parse_emit(arg.substr(std::strlen("--emit=")));

            if (!emit) {
                parsed.exit_status =
                    usage_error("unknown output kind '" + std::string(arg.substr(7)) +
                                "': expected exe, obj, asm, bc or ll");
                return parsed;
            }

            options.emit = *emit;
        } else if (arg == "--keep") {
            options.keep = true;
        } else if (starts_with(arg, "--cpu=")) {
            options.cpu = std::string(arg.substr(std::strlen("--cpu=")));
        } else if (starts_with(arg, "--sysroot=")) {
            options.sysroot = std::string(arg.substr(std::strlen("--sysroot=")));
        } else if (starts_with(arg, "-L")) {
            if (!value_of(i, "-L", value)) {
                parsed.exit_status = usage_error("-L needs a directory");
                return parsed;
            }

            options.library_paths.push_back(value);
        } else if (starts_with(arg, "-l")) {
            if (!value_of(i, "-l", value)) {
                parsed.exit_status = usage_error("-l needs a library name");
                return parsed;
            }

            options.libraries.push_back(value);
        } else if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
        } else if (starts_with(arg, "-") && arg != "-") {
            parsed.exit_status = usage_error("unknown option '" + std::string(arg) + "'");
            return parsed;
        } else if (ends_with(arg, ".o") || ends_with(arg, ".a")) {
            options.link_inputs.emplace_back(arg);
        } else if (options.input.empty()) {
            options.input = std::string(arg);
        } else {
            parsed.exit_status = usage_error("more than one IR input: '" + options.input +
                                             "' and '" + std::string(arg) + "'");
            return parsed;
        }
    }

    if (options.input.empty()) {
        parsed.exit_status = usage_error("no input");
        return parsed;
    }

    if (options.output.empty()) {
        parsed.exit_status = usage_error("no output: give one with -o");
        return parsed;
    }

    if (options.emit != Emit::Executable &&
        (!options.link_inputs.empty() || !options.libraries.empty())) {
        parsed.exit_status =
            usage_error("objects, archives and libraries are only used when linking an executable");
        return parsed;
    }

    parsed.options = std::move(options);
    return parsed;
}

} // namespace ghul_llvm
