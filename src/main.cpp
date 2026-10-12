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
    // makes the linker pull it into the executable.
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    llvm::SmallVector<char, 0> object;

    if (!ghul_llvm::compile(options, ghul_llvm::sysroot_of(options, argv[0]), object)) {
        return 1;
    }

    if (options.emit != ghul_llvm::Emit::Executable) {
        return 0;
    }

    return ghul_llvm::link(options, llvm::StringRef(object.data(), object.size()), argv[0]) ? 0
                                                                                            : 1;
}
