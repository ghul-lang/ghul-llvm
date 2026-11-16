// ghul-llvm: assembles, optimises and compiles the LLVM IR the ghūl compiler
// writes, and links it with the C runtime into a statically linked
// executable. See README.md.

#include "compile.h"
#include "libraries.h"
#include "link.h"
#include "options.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"

int main(int argc, char **argv) {
    llvm::InitLLVM init(argc, argv);

    auto parsed = ghul_llvm::parse_options(argc, argv);

    if (!parsed.options) {
        return parsed.exit_status;
    }

    const auto &options = *parsed.options;

    // Tab separated, for a build tool to read: the file, its soname and the
    // version its file name carries, either of the last two possibly empty.
    if (options.find_library) {
        auto library = ghul_llvm::find_library(*options.find_library, options.library_paths);

        if (!library) {
            llvm::errs() << "ghul-llvm: no shared library lib" << *options.find_library
                         << ".so for this machine was found\n";
            return 1;
        }

        llvm::outs() << library->path << "\t" << library->soname << "\t" << library->version << "\n";
        return 0;
    }

    // Only the target this build generates code for is initialised. LLVM's
    // release libraries carry every backend, and initialising one is what
    // makes the linker pull it into the executable. The build defines
    // GHUL_LLVM_BACKEND_ID as the backend's name unquoted, so a token paste
    // names the one real initialiser the stubs do not shadow.
#define GHUL_LLVM_CONCAT(a, b) GHUL_LLVM_CONCAT2(a, b)
#define GHUL_LLVM_CONCAT2(a, b) a##b
#define GHUL_LLVM_INITIALIZE(part) GHUL_LLVM_CONCAT(LLVMInitialize, GHUL_LLVM_CONCAT(GHUL_LLVM_BACKEND_ID, part))

    GHUL_LLVM_INITIALIZE(TargetInfo)();
    GHUL_LLVM_INITIALIZE(Target)();
    GHUL_LLVM_INITIALIZE(TargetMC)();
    GHUL_LLVM_INITIALIZE(AsmParser)();
    GHUL_LLVM_INITIALIZE(AsmPrinter)();

    llvm::SmallVector<char, 0> object;

    if (!ghul_llvm::compile(options, object)) {
        return 1;
    }

    if (options.emit == ghul_llvm::Emit::Cell) {
        return ghul_llvm::link_cell(options, llvm::StringRef(object.data(), object.size())) ? 0 : 1;
    }

    if (options.emit != ghul_llvm::Emit::Executable) {
        return 0;
    }

    llvm::StringRef linked(object.data(), object.size());

    if (options.dynamic) {
        return ghul_llvm::link_dynamic(options, linked, argv[0]) ? 0 : 1;
    }

    return ghul_llvm::link(options, linked, argv[0]) ? 0 : 1;
}
