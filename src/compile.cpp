#include "compile.h"

#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
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

std::unique_ptr<llvm::TargetMachine> target_machine(const Options &options) {
    llvm::Triple triple(GHUL_LLVM_TRIPLE);
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
    enable_implicit_null_checks();

    // Executables are linked statically and not as position independent,
    // so code is generated for a fixed load address.
    return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
        triple, options.cpu, "", target_options,
        llvm::Reloc::Static, std::nullopt, codegen_level(options.optimisation)));
}

// Gives the module the triple and data layout this build generates code
// for, and refuses one that names a different triple. A module that names
// none takes ours.
bool set_target(llvm::Module &module, llvm::TargetMachine &machine) {
    const llvm::Triple &ours = machine.getTargetTriple();
    const llvm::Triple &theirs = module.getTargetTriple();

    if (!theirs.str().empty() && theirs.normalize() != ours.normalize()) {
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

// The collector's allocators, as the optimiser is to see them once libghul's
// allocation functions are inlined into the program: each answers a fresh
// object of the size it is given, or does not return. GC_malloc clears what
// it answers; GC_malloc_atomic does not.
void describe_allocator(llvm::Module &module, llvm::StringRef name, llvm::StringRef kind) {
    llvm::Function *allocator = module.getFunction(name);

    if (!allocator || !allocator->isDeclaration()) {
        return;
    }

    llvm::LLVMContext &context = module.getContext();

    allocator->addFnAttr(llvm::Attribute::getWithAllocKind(
        context, kind == "zeroed"
                     ? llvm::AllocFnKind::Alloc | llvm::AllocFnKind::Zeroed
                     : llvm::AllocFnKind::Alloc | llvm::AllocFnKind::Uninitialized));
    allocator->addFnAttr("alloc-family", "ghul_gc");
    allocator->addFnAttr(llvm::Attribute::getWithAllocSizeArgs(context, 0, std::nullopt));
    allocator->addFnAttr(llvm::Attribute::NoUnwind);
    allocator->addRetAttr(llvm::Attribute::NoAlias);
    allocator->addRetAttr(llvm::Attribute::NonNull);
}

// The symbols the objects and archives linked beside the program leave
// undefined, which libghul may be what defines.
bool undefined_in_link_inputs(const Options &options, llvm::StringSet<> &names) {
    auto add_object = [&](const llvm::object::ObjectFile &object) {
        for (const llvm::object::SymbolRef &symbol : object.symbols()) {
            auto flags = symbol.getFlags();
            auto name = symbol.getName();

            if (flags && name && (*flags & llvm::object::SymbolRef::SF_Undefined)) {
                names.insert(*name);
            } else {
                llvm::consumeError(flags.takeError());
                llvm::consumeError(name.takeError());
            }
        }
    };

    for (const std::string &input : options.link_inputs) {
        auto binary = llvm::object::createBinary(input);

        if (!binary) {
            return fail("cannot read " + input + ": " + llvm::toString(binary.takeError()));
        }

        if (auto *object = llvm::dyn_cast<llvm::object::ObjectFile>(binary->getBinary())) {
            add_object(*object);
        } else if (auto *archive = llvm::dyn_cast<llvm::object::Archive>(binary->getBinary())) {
            llvm::Error error = llvm::Error::success();

            for (const llvm::object::Archive::Child &child : archive->children(error)) {
                auto member = child.getAsBinary();

                if (!member) {
                    llvm::consumeError(member.takeError());
                    continue;
                }

                if (auto *object = llvm::dyn_cast<llvm::object::ObjectFile>(member->get())) {
                    add_object(*object);
                }
            }

            if (error) {
                return fail("cannot read " + input + ": " + llvm::toString(std::move(error)));
            }
        }
    }

    return true;
}

// A member of libghul's archive: one source file's bitcode, and the
// external symbols it defines.
struct Member {
    std::unique_ptr<llvm::Module> module;
    std::vector<std::string> defines;
};

bool read_libghul(const std::string &path, llvm::LLVMContext &context,
                  std::vector<Member> &members) {
    auto buffer = llvm::MemoryBuffer::getFile(path);

    if (!buffer) {
        return fail("cannot read " + path + ": " + buffer.getError().message() +
                    "; give the C runtime's location with --sysroot");
    }

    auto archive = llvm::object::Archive::create((*buffer)->getMemBufferRef());

    if (!archive) {
        return fail("cannot read " + path + ": " + llvm::toString(archive.takeError()));
    }

    llvm::Error error = llvm::Error::success();

    for (const llvm::object::Archive::Child &child : (*archive)->children(error)) {
        auto contents = child.getMemoryBufferRef();

        if (!contents) {
            return fail("cannot read " + path + ": " + llvm::toString(contents.takeError()));
        }

        auto module = llvm::parseBitcodeFile(*contents, context);

        if (!module) {
            return fail("cannot read " + path + ": " + llvm::toString(module.takeError()));
        }

        Member member;

        for (llvm::GlobalValue &value : (*module)->global_values()) {
            if (!value.isDeclaration() && !value.hasLocalLinkage()) {
                member.defines.push_back(value.getName().str());
            }
        }

        // libghul is built for the target in general; its functions are
        // generated for the CPU the program is.
        for (llvm::Function &function : **module) {
            function.removeFnAttr("target-cpu");
            function.removeFnAttr("target-features");
            function.removeFnAttr("tune-cpu");
        }

        member.module = std::move(*module);
        members.push_back(std::move(member));
    }

    if (error) {
        return fail("cannot read " + path + ": " + llvm::toString(std::move(error)));
    }

    return true;
}

// Whether the program's module needs a definition of `name`: it refers to
// it other than weakly, or the objects linked beside it do, and it has none.
bool is_wanted(const llvm::Module &module, const std::string &name,
               const llvm::StringSet<> &wanted_elsewhere) {
    const llvm::GlobalValue *value = module.getNamedValue(name);

    if (!value) {
        return wanted_elsewhere.contains(name);
    }

    return value->isDeclaration() && !value->hasExternalWeakLinkage();
}

// Links libghul's bitcode into the program's module, so that its functions
// are optimised with the program: inlined where they are called and their
// effects known to the program's code. Members are taken as a static linker
// takes an archive's: one that defines something the program or the objects
// linked beside it refer to, other than weakly, and main unless the program
// defines its own, until nothing more is wanted.
bool link_libghul(const Options &options, llvm::Module &module, const std::string &sysroot) {
    llvm::SmallString<256> path(sysroot);
    llvm::sys::path::append(path, "libghul.a");

    std::vector<Member> members;

    if (!read_libghul(std::string(path), module.getContext(), members)) {
        return false;
    }

    llvm::StringSet<> wanted_elsewhere;

    if (!undefined_in_link_inputs(options, wanted_elsewhere)) {
        return false;
    }

    wanted_elsewhere.insert("main");

    llvm::Linker linker(module);

    for (bool linked = true; linked;) {
        linked = false;

        for (Member &member : members) {
            if (!member.module) {
                continue;
            }

            bool wanted = false;

            for (const std::string &name : member.defines) {
                wanted = wanted || is_wanted(module, name, wanted_elsewhere);
            }

            if (!wanted) {
                continue;
            }

            member.module->setTargetTriple(module.getTargetTriple());
            member.module->setDataLayout(module.getDataLayout());

            if (linker.linkInModule(std::move(member.module))) {
                return fail("cannot link " + path + " into " + module.getModuleIdentifier());
            }

            linked = true;
        }
    }

    describe_allocator(module, "GC_malloc", "zeroed");
    describe_allocator(module, "GC_malloc_atomic", "uninitialized");

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

bool compile(const Options &options, const std::string &sysroot,
             llvm::SmallVectorImpl<char> &object) {
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

    auto machine = target_machine(options);

    if (!machine || !set_target(*module, *machine)) {
        return false;
    }

    // Only an executable is a whole program: an object or IR is linked into
    // one later, by a build that links libghul itself.
    if (options.emit == Emit::Executable && !link_libghul(options, *module, sysroot)) {
        return false;
    }

    if (!optimise(options, *module, *machine)) {
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
