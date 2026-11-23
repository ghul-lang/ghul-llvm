#include "link.h"

#include "libraries.h"

#include "lld/Common/Driver.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <vector>

LLD_HAS_DRIVER(elf)

namespace ghul_llvm {

namespace {

// The dynamic loader a program linked against glibc names, which each
// architecture's psABI fixes.
#if defined(__x86_64__)
#define GHUL_LLVM_DYNAMIC_LOADER "/lib64/ld-linux-x86-64.so.2"
#elif defined(__aarch64__)
#define GHUL_LLVM_DYNAMIC_LOADER "/lib/ld-linux-aarch64.so.1"
#else
#error "no dynamic loader is known for this architecture"
#endif

bool fail(const llvm::Twine &message) {
    llvm::errs() << "ghul-llvm: " << message << "\n";
    return false;
}

// The C runtime ships in the release archive as lib/<triple>, beside the bin
// directory the executable is in: the musl triple's for a static link, and
// the same machine's gnu triple for a dynamic one.
std::string default_sysroot(const char *argv0, bool dynamic) {
    static int anchor;
    std::string executable = llvm::sys::fs::getMainExecutable(argv0, &anchor);

    std::string triple = GHUL_LLVM_TRIPLE;

    if (dynamic) {
        const std::string musl = "-musl";
        triple.replace(triple.rfind(musl), musl.size(), "-gnu");
    }

    llvm::SmallString<256> path(llvm::sys::path::parent_path(executable));
    llvm::sys::path::remove_filename(path);
    llvm::sys::path::append(path, "lib", triple);

    return std::string(path);
}

// Checks every file in `needed` is in `sysroot`.
bool check_sysroot(const std::string &sysroot, llvm::ArrayRef<const char *> needed) {
    for (const char *file : needed) {
        llvm::SmallString<256> path(sysroot);
        llvm::sys::path::append(path, file);

        if (!llvm::sys::fs::exists(path)) {
            return fail("the C runtime is not in " + sysroot + ": " + file +
                        " is missing; give its location with --sysroot");
        }
    }

    return true;
}

std::string in_directory(const std::string &directory, const char *file) {
    llvm::SmallString<256> path(directory);
    llvm::sys::path::append(path, file);
    return std::string(path);
}

// Removes a temporary file when it goes out of scope, unless kept.
class TemporaryFile {
  public:
    explicit TemporaryFile(std::string path, bool keep) : _path(std::move(path)), _keep(keep) {}

    ~TemporaryFile() {
        if (!_keep && !_path.empty()) {
            llvm::sys::fs::remove(_path);
        }
    }

    TemporaryFile(const TemporaryFile &) = delete;
    TemporaryFile &operator=(const TemporaryFile &) = delete;

    const std::string &path() const { return _path; }

  private:
    std::string _path;
    bool _keep;
};

// LLD reads its inputs from files, so the object goes to one: beside the
// output when it is to be kept, and otherwise a temporary one, whose path
// `path` is set to.
bool write_object(const Options &options, llvm::StringRef object, std::string &path) {
    if (options.keep) {
        path = options.output + ".o";
    } else {
        llvm::SmallString<256> temporary;

        if (auto error = llvm::sys::fs::createTemporaryFile("ghul-llvm", "o", temporary)) {
            return fail("cannot create a temporary file: " + error.message());
        }

        path = std::string(temporary);
    }

    std::error_code error;
    llvm::raw_fd_ostream out(path, error, llvm::sys::fs::OF_None);

    if (error) {
        return fail("cannot write " + path + ": " + error.message());
    }

    out << object;
    out.close();

    if (out.has_error()) {
        return fail("cannot write " + path + ": " + out.error().message());
    }

    return true;
}

bool run_lld(const Options &options, const std::vector<std::string> &args) {
    if (options.verbose) {
        for (size_t i = 0; i < args.size(); ++i) {
            llvm::errs() << (i ? " " : "") << args[i];
        }

        llvm::errs() << "\n";
    }

    std::vector<const char *> argv;

    for (const auto &arg : args) {
        argv.push_back(arg.c_str());
    }

    lld::Result result =
        lld::lldMain(argv, llvm::outs(), llvm::errs(), {{lld::Gnu, &lld::elf::link}});

    return result.retCode == 0;
}

} // namespace

bool link(const Options &options, llvm::StringRef object, const char *argv0) {
    std::string sysroot = options.sysroot ? *options.sysroot : default_sysroot(argv0, false);

    const char *collector = "libgc-parallel.a";
    const char *needed[] = {"crt1.o",  "crti.o", "crtn.o",
                             "libghul.a", "ghul.ld", "libtommath.a", collector, "libbacktrace.a", "libunwind.a", "libc.a",
                             "libclang_rt.builtins.a"};

    if (!check_sysroot(sysroot, needed)) {
        return false;
    }

    auto in_sysroot = [&](const char *file) { return in_directory(sysroot, file); };

    std::string object_path;

    if (!write_object(options, object, object_path)) {
        return false;
    }

    TemporaryFile object_file(object_path, options.keep);

    // The order is the one a C compiler driver gives a static link: start
    // files, the program, its libraries, then libghul, the
    // collector, the unwinder, the C library and the builtins, grouped so that each can
    // use the others, and the closing start file. An archive member is only
    // taken when something refers to it, so a program that defines its own
    // main takes nothing from libghul.
    // Nothing is searched for outside the sysroot and the -L directories.
    std::vector<std::string> args = {
        "ld.lld",
        "-static",
        "--gc-sections",
        // The unwinder finds a function's unwind table through this index.
        "--eh-frame-hdr",
        "-z", "noexecstack",
        "-o", options.output,
        in_sysroot("crt1.o"),
        in_sysroot("crti.o"),
        object_path,
    };

    for (const auto &input : options.link_inputs) {
        args.push_back(input);
    }

    for (const auto &path : options.library_paths) {
        args.push_back("-L" + path);
    }

    args.push_back("--start-group");

    for (const auto &library : options.libraries) {
        args.push_back("-l" + library);
    }

    args.push_back(in_sysroot("libghul.a"));
    args.push_back(in_sysroot("libtommath.a"));
    args.push_back(in_sysroot(collector));
    args.push_back(in_sysroot("libbacktrace.a"));
    args.push_back(in_sysroot("libunwind.a"));
    args.push_back(in_sysroot("libc.a"));
    args.push_back(in_sysroot("libclang_rt.builtins.a"));
    args.push_back("--end-group");
    args.push_back(in_sysroot("crtn.o"));

    // Keeps the fault map --gc-sections would otherwise drop; see
    // libghul/ghul.ld.
    args.push_back(in_sysroot("ghul.ld"));

    return run_lld(options, args);
}

bool link_dynamic(const Options &options, llvm::StringRef object, const char *argv0) {
    std::string sysroot = options.sysroot ? *options.sysroot : default_sysroot(argv0, true);

    const char *needed[] = {"ghul-start.o", "libghul.a", "ghul.ld", "libtommath.a", "libgc.a",
                            "libbacktrace.a", "libunwind.a", "libclang_rt.builtins.a"};

    if (!check_sysroot(sysroot, needed)) {
        return false;
    }

    auto in_sysroot = [&](const char *file) { return in_directory(sysroot, file); };

    // The C library is the machine's: libc.so.6 and libm.so.6, named by
    // their files, so the linker reads neither the development package's
    // libc.so linker script nor anything else of the system's.
    std::vector<SharedLibrary> system;

    for (const char *soname : {"libm.so.6", "libc.so.6"}) {
        auto library = find_soname(soname);

        if (!library) {
            return fail(std::string("linking dynamically needs the system's ") + soname +
                        ", which was not found in /etc/ld.so.cache or the standard library directories");
        }

        system.push_back(*library);
    }

    // Each library named with -l is a shared library of the system's, or
    // one in a -L directory.
    std::vector<SharedLibrary> named;

    for (const auto &name : options.libraries) {
        auto library = find_library(name, options.library_paths);

        if (!library) {
            return fail("no shared library lib" + name + ".so for this machine was found in the -L "
                        "directories, /etc/ld.so.cache or the standard library directories");
        }

        named.push_back(*library);
    }

    std::string object_path;

    if (!write_object(options, object, object_path)) {
        return false;
    }

    TemporaryFile object_file(object_path, options.keep);

    // Linked for a fixed load address, as the code is generated, with the
    // loader the psABI names. The program's own start object comes first,
    // then the program and its inputs, then the C runtime grouped so each
    // part can use the others, then the shared libraries. A shared library
    // nothing refers to is not recorded as needed. What the C runtime
    // defines stays out of the dynamic symbol table, so a shared library
    // that brings an unwinder of its own keeps to it. The shared libraries'
    // own dependencies are the dynamic loader's to find when the program
    // starts, so the link does not look for them.
    std::vector<std::string> args = {
        "ld.lld",
        "--no-pie",
        "--dynamic-linker", GHUL_LLVM_DYNAMIC_LOADER,
        "--gc-sections",
        "--eh-frame-hdr",
        "--allow-shlib-undefined",
        "--exclude-libs", "ALL",
        "-z", "noexecstack",
        "-z", "relro",
        "-z", "now",
        "-o", options.output,
        in_sysroot("ghul-start.o"),
        object_path,
    };

    for (const auto &input : options.link_inputs) {
        args.push_back(input);
    }

    // Archives a -L directory holds are still found by name.
    for (const auto &path : options.library_paths) {
        args.push_back("-L" + path);
    }

    args.push_back("--start-group");
    args.push_back(in_sysroot("libghul.a"));
    args.push_back(in_sysroot("libtommath.a"));
    args.push_back(in_sysroot("libgc.a"));
    args.push_back(in_sysroot("libbacktrace.a"));
    args.push_back(in_sysroot("libunwind.a"));
    args.push_back(in_sysroot("libclang_rt.builtins.a"));
    args.push_back("--end-group");

    args.push_back("--as-needed");

    for (const auto &library : named) {
        args.push_back(library.path);

        // A library from a -L directory is where the loader would not look,
        // so the program says where.
        if (library.from_library_path) {
            llvm::SmallString<256> directory(llvm::sys::path::parent_path(library.path));
            llvm::sys::fs::make_absolute(directory);
            args.push_back("-rpath");
            args.push_back(std::string(directory));
        }
    }

    for (const auto &library : system) {
        args.push_back(library.path);
    }

    args.push_back(in_sysroot("ghul.ld"));

    return run_lld(options, args);
}

bool link_cell(const Options &options, llvm::StringRef object) {
    llvm::SmallString<256> path;

    if (auto error = llvm::sys::fs::createTemporaryFile("ghul-llvm", "o", path)) {
        return fail("cannot create a temporary file: " + error.message());
    }

    TemporaryFile object_file(std::string(path), options.keep);

    {
        std::error_code error;
        llvm::raw_fd_ostream out(object_file.path(), error, llvm::sys::fs::OF_None);

        if (error) {
            return fail("cannot write " + object_file.path() + ": " + error.message());
        }

        out << object;
        out.close();

        if (out.has_error()) {
            return fail("cannot write " + object_file.path() + ": " + out.error().message());
        }
    }

    // A shared object with no start files and no libraries. Every default
    // visibility name it defines is exported, and none binds to itself, so
    // that a name another object defines first still wins.
    std::vector<std::string> args = {
        "ld.lld",
        "-shared",
        "--eh-frame-hdr",
        "-z", "noexecstack",
        "-o", options.output,
        object_file.path(),
    };

    return run_lld(options, args);
}

} // namespace ghul_llvm
