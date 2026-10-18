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

    if (options.emit != ghul_llvm::Emit::Executable) {
        return 0;
    }

    return ghul_llvm::link(options, llvm::StringRef(object.data(), object.size()), argv[0]) ? 0
                                                                                            : 1;
}
