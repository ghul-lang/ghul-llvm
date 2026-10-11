#include "link.h"

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

bool fail(const llvm::Twine &message) {
    llvm::errs() << "ghul-llvm: " << message << "\n";
    return false;
}

// The C runtime ships in the release archive as lib/<triple>, beside the bin
// directory the executable is in.
std::string default_sysroot(const char *argv0) {
    static int anchor;
    std::string executable = llvm::sys::fs::getMainExecutable(argv0, &anchor);

    llvm::SmallString<256> path(llvm::sys::path::parent_path(executable));
    llvm::sys::path::remove_filename(path);
    llvm::sys::path::append(path, "lib", GHUL_LLVM_TRIPLE);

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

} // namespace

bool link(const Options &options, llvm::StringRef object, const char *argv0) {
    std::string sysroot = options.sysroot ? *options.sysroot : default_sysroot(argv0);

    const char *runtime[] = {"crt1.o", "crti.o", "crtn.o", "libc.a", "libclang_rt.builtins.a"};

    for (const char *file : runtime) {
        llvm::SmallString<256> path(sysroot);
        llvm::sys::path::append(path, file);

        if (!llvm::sys::fs::exists(path)) {
            return fail("the C runtime is not in " + sysroot + ": " + file +
                        " is missing; give its location with --sysroot");
        }
    }

    auto in_sysroot = [&](const char *file) {
        llvm::SmallString<256> path(sysroot);
        llvm::sys::path::append(path, file);
        return std::string(path);
    };

    // LLD reads its inputs from files, so the object goes to one: beside the
    // output when it is to be kept, and otherwise a temporary one.
    std::string object_path;

    if (options.keep) {
        object_path = options.output + ".o";
    } else {
        llvm::SmallString<256> path;

        if (auto error = llvm::sys::fs::createTemporaryFile("ghul-llvm", "o", path)) {
            return fail("cannot create a temporary file: " + error.message());
        }

        object_path = std::string(path);
    }

    TemporaryFile object_file(object_path, options.keep);

    {
        std::error_code error;
        llvm::raw_fd_ostream out(object_path, error, llvm::sys::fs::OF_None);

        if (error) {
            return fail("cannot write " + object_path + ": " + error.message());
        }

        out << object;
        out.close();

        if (out.has_error()) {
            return fail("cannot write " + object_path + ": " + out.error().message());
        }
    }

    // The order is the one a C compiler driver gives a static link: start
    // files, the program, its libraries, the C library and the builtins
    // grouped so that each can use the others, and the closing start file.
    // Nothing is searched for outside the sysroot and the -L directories.
    std::vector<std::string> args = {
        "ld.lld",
        "-static",
        "--gc-sections",
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

    args.push_back(in_sysroot("libc.a"));
    args.push_back(in_sysroot("libclang_rt.builtins.a"));
    args.push_back("--end-group");
    args.push_back(in_sysroot("crtn.o"));

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

} // namespace ghul_llvm
