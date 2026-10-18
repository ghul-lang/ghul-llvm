// ghul-llvm: assembles, optimises and compiles the LLVM IR the ghūl compiler
// writes, and links it with the C runtime into a statically linked
// executable. See README.md.

#include "compile.h"
#include "link.h"
#include "options.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"

int main(int argc, char **argv) {
    llvm::InitLLVM init(argc, argv);

    auto parsed = ghul_llvm::parse_options(argc, argv);

    if (!parsed.options) {
        return parsed.exit_status;
    }

    const auto &options = *parsed.options;

    // Only the target this build generates code for is initialised. LLVM's
    // release libraries carry every backend, and initialising one is what
    // makes the linker pull it into the executable. The build defines
    // GHUL_LLVM_BACKEND as the name LLVM registers the triple's target
    // under, and a macro-quoted call names the one real initialiser the
    // stubs do not shadow.
#define GHUL_LLVM_INITIALIZE(backend, part) GHUL_LLVM_CONCAT(LLVMInitialize, GHUL_LLVM_CONCAT(backend, part))
#define GHUL_LLVM_CONCAT(a, b) GHUL_LLVM_CONCAT2(a, b)
#define GHUL_LLVM_CONCAT2(a, b) a##b

    GHUL_LLVM_INITIALIZE(GHUL_LLVM_BACKEND, TargetInfo)();
    GHUL_LLVM_INITIALIZE(GHUL_LLVM_BACKEND, Target)();
    GHUL_LLVM_INITIALIZE(GHUL_LLVM_BACKEND, TargetMC)();
    GHUL_LLVM_INITIALIZE(GHUL_LLVM_BACKEND, AsmParser)();
    GHUL_LLVM_INITIALIZE(GHUL_LLVM_BACKEND, AsmPrinter)();

    llvm::SmallVector<char, 0> object;

    if (!ghul_llvm::compile(options, object)) {
        return 1;
    }

    if (options.emit != ghul_llvm::Emit::Executable) {
        return 0;
    }

    return ghul_llvm::link(options, llvm::StringRef(object.data(), object.size()), argv[0]) ? 0
                                                                                            : 1;
}
