#include "compile.h"

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

#include <memory>

namespace ghul_llvm {

namespace {

bool fail(const llvm::Twine &message) {
    llvm::errs() << "ghul-llvm: " << message << "\n";
    return false;
}

// LLVM has no API for the pass, only the command-line option that adds it to
// the code generation pipeline at every level but -O0.
void enable_implicit_null_checks() {
    auto &registered = llvm::cl::getRegisteredOptions();
    auto found = registered.find("enable-implicit-null-checks");

    if (found != registered.end()) {
        static_cast<llvm::cl::opt<bool> *>(found->second)->setValue(true);
    }
}

bool write_file(const std::string &path, llvm::StringRef content) {
    std::error_code error;
    llvm::raw_fd_ostream out(path, error, llvm::sys::fs::OF_None);

    if (error) {
        return fail("cannot write " + path + ": " + error.message());
    }

    out << content;
    out.close();

    if (out.has_error()) {
        return fail("cannot write " + path + ": " + out.error().message());
    }

    return true;
}

llvm::SmallVector<char, 0> bitcode_of(const llvm::Module &module) {
    llvm::SmallVector<char, 0> bitcode;
    llvm::BitcodeWriter writer(bitcode);
    writer.writeModule(module);
    writer.writeSymtab();
    writer.writeStrtab();
    return bitcode;
}

llvm::OptimizationLevel pass_level(unsigned level) {
    switch (level) {
    case 0: return llvm::OptimizationLevel::O0;
    case 1: return llvm::OptimizationLevel::O1;
    case 3: return llvm::OptimizationLevel::O3;
    default: return llvm::OptimizationLevel::O2;
    }
}

llvm::CodeGenOptLevel codegen_level(unsigned level) {
    switch (level) {
    case 0: return llvm::CodeGenOptLevel::None;
    case 1: return llvm::CodeGenOptLevel::Less;
    case 3: return llvm::CodeGenOptLevel::Aggressive;
    default: return llvm::CodeGenOptLevel::Default;
    }
}

// A cell runs inside a glibc host, so its code is generated for the same
// machine under the gnu environment.
std::string target_triple(const Options &options) {
    std::string triple = GHUL_LLVM_TRIPLE;

    if (options.emit == Emit::Cell) {
        const std::string musl = "-musl";
        triple.replace(triple.rfind(musl), musl.size(), "-gnu");
    }

    return triple;
}

std::unique_ptr<llvm::TargetMachine> target_machine(const Options &options) {
    llvm::Triple triple(target_triple(options));
    std::string error;

    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, error);

    if (!target) {
        fail(error);
        return nullptr;
    }

    // One section per function and per global, so the linker's
    // --gc-sections can drop whatever the program does not reach.
    llvm::TargetOptions target_options;
    target_options.FunctionSections = true;
    target_options.DataSections = true;

    // A test for null that generated code marks !make.implicit is folded,
    // where it can be, into the read it guards: the read is left to fault,
    // and the fault map records where the test would have branched to.
    // libghul resumes there when the read faults.
    // A cell's fault map is not one libghul in the host knows of, so a cell
    // keeps its explicit null tests.
    if (options.emit != Emit::Cell) {
        enable_implicit_null_checks();
    }

    // Executables are linked statically and not as position independent,
    // so code is generated for a fixed load address. A cell is loaded
    // wherever the host puts it and binds its external names to the host's
    // and to other cells', so its code is position independent.
    return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
        triple, options.cpu, "", target_options,
        options.emit == Emit::Cell ? llvm::Reloc::PIC_ : llvm::Reloc::Static, std::nullopt, codegen_level(options.optimisation)));
}

// Gives the module the triple and data layout this build generates code
// for, and refuses one that names a different triple. A module that names
// none takes ours.
bool set_target(const Options &options, llvm::Module &module, llvm::TargetMachine &machine) {
    const llvm::Triple &ours = machine.getTargetTriple();
    const llvm::Triple &theirs = module.getTargetTriple();

    // A cell may be written for the musl triple the compiler names: only the
    // machine matters, since the libc is the host's.
    bool same = options.emit == Emit::Cell ? theirs.getArch() == ours.getArch()
                                           : theirs.normalize() == ours.normalize();

    if (!theirs.str().empty() && !same) {
        return fail(module.getModuleIdentifier() + " is for " + theirs.str() +
                    ", but ghul-llvm generates code for " + ours.str());
    }

    module.setTargetTriple(ours);

    llvm::DataLayout layout = machine.createDataLayout();

    if (!module.getDataLayoutStr().empty() && module.getDataLayout() != layout) {
        return fail(module.getModuleIdentifier() + " has data layout '" +
                    module.getDataLayoutStr() + "', but " + ours.str() + " uses '" +
                    layout.getStringRepresentation() + "'");
    }

    module.setDataLayout(layout);
    return true;
}

bool optimise(const Options &options, llvm::Module &module, llvm::TargetMachine &machine) {
    llvm::LoopAnalysisManager loops;
    llvm::FunctionAnalysisManager functions;
    llvm::CGSCCAnalysisManager cgscc;
    llvm::ModuleAnalysisManager modules;

    llvm::PassBuilder builder(&machine);

    builder.registerModuleAnalyses(modules);
    builder.registerCGSCCAnalyses(cgscc);
    builder.registerFunctionAnalyses(functions);
    builder.registerLoopAnalyses(loops);
    builder.crossRegisterProxies(loops, functions, cgscc, modules);

    llvm::ModulePassManager passes;

    if (options.passes) {
        if (auto error = builder.parsePassPipeline(passes, *options.passes)) {
            return fail("invalid pass pipeline '" + *options.passes +
                        "': " + llvm::toString(std::move(error)));
        }
    } else if (options.optimisation == 0) {
        passes = builder.buildO0DefaultPipeline(llvm::OptimizationLevel::O0);
    } else {
        passes = builder.buildPerModuleDefaultPipeline(pass_level(options.optimisation));
    }

    passes.run(module, modules);

    // The default pipelines leave valid IR behind them; a pipeline given
    // with --passes need not, and code generation is no place to find out.
    if (llvm::verifyModule(module, &llvm::errs())) {
        return fail("the IR is not valid after optimisation");
    }

    return true;
}

bool generate(llvm::Module &module, llvm::TargetMachine &machine, llvm::CodeGenFileType kind,
              llvm::SmallVectorImpl<char> &output) {
    llvm::raw_svector_ostream stream(output);
    llvm::legacy::PassManager passes;

    if (machine.addPassesToEmitFile(passes, stream, nullptr, kind)) {
        return fail("the target cannot emit this kind of file");
    }

    passes.run(module);
    return true;
}

} // namespace

bool compile(const Options &options, llvm::SmallVectorImpl<char> &object) {
    llvm::LLVMContext context;
    llvm::SMDiagnostic diagnostic;

    // Reads either form: text IR or bitcode.
    std::unique_ptr<llvm::Module> module =
        llvm::parseIRFile(options.input, diagnostic, context);

    if (!module) {
        diagnostic.print("ghul-llvm", llvm::errs());
        return false;
    }

    if (llvm::verifyModule(*module, &llvm::errs())) {
        return fail(options.input + " is not valid IR");
    }

    // Every function gets an unwind table, including those that cannot
    // throw, so a crash report can walk the stack through any frame. The
    // tables are data beside the code and change no instruction.
    if (options.emit == Emit::Cell) {
        module->setPICLevel(llvm::PICLevel::BigPIC);
    }

    if (module->getUwtable() == llvm::UWTableKind::None) {
        module->setUwtable(llvm::UWTableKind::Async);
    }

    for (llvm::Function &function : *module) {
        if (!function.isDeclaration()) {
            function.setUWTableKind(llvm::UWTableKind::Async);
        }
    }

    auto machine = target_machine(options);

    if (!machine || !set_target(options, *module, *machine) || !optimise(options, *module, *machine)) {
        return false;
    }

    switch (options.emit) {
    case Emit::IR: {
        std::string text;
        llvm::raw_string_ostream stream(text);
        module->print(stream, nullptr);
        return write_file(options.output, text);
    }

    case Emit::Bitcode: {
        auto bitcode = bitcode_of(*module);
        return write_file(options.output, llvm::StringRef(bitcode.data(), bitcode.size()));
    }

    case Emit::Assembly: {
        llvm::SmallVector<char, 0> assembly;
        return generate(*module, *machine, llvm::CodeGenFileType::AssemblyFile, assembly) &&
               write_file(options.output, llvm::StringRef(assembly.data(), assembly.size()));
    }

    case Emit::Cell:
        return generate(*module, *machine, llvm::CodeGenFileType::ObjectFile, object);

    case Emit::Object:
        return generate(*module, *machine, llvm::CodeGenFileType::ObjectFile, object) &&
               write_file(options.output, llvm::StringRef(object.data(), object.size()));

    case Emit::Executable:
        if (options.keep) {
            auto bitcode = bitcode_of(*module);

            if (!write_file(options.output + ".bc",
                            llvm::StringRef(bitcode.data(), bitcode.size()))) {
                return false;
            }
        }

        return generate(*module, *machine, llvm::CodeGenFileType::ObjectFile, object);
    }

    return false;
}

} // namespace ghul_llvm
