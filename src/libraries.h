#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ghul_llvm {

// A shared library found for a dynamic link.
struct SharedLibrary {
    // The file to link: the runtime file named by the library's soname where
    // there is one (libz.so.1), so that only the runtime package need be
    // installed, not the development package's libz.so.
    std::string path;

    // The library's DT_SONAME, which the program records as the library it
    // needs, or empty where it has none.
    std::string soname;

    // The version the real file's name carries after ".so." (1.3.1 for
    // libz.so.1.3.1), or empty where it carries none. This is the library's
    // ABI version, which is its release's version for some libraries only.
    std::string version;

    // Whether it was found in one of the directories given with -L rather
    // than where the dynamic loader looks, so the program must be told where
    // it is.
    bool from_library_path = false;
};

// Finds lib<name>.so for this machine's architecture: first in each of
// `library_paths`, in order, then in the dynamic loader's cache,
// /etc/ld.so.cache, then in the standard library directories. Where several
// files are the library, the one with the highest soname wins.
std::optional<SharedLibrary> find_library(const std::string &name,
                                          const std::vector<std::string> &library_paths);

// Finds the library whose soname is `soname` (libc.so.6), where the dynamic
// loader looks: its cache, then the standard library directories.
std::optional<SharedLibrary> find_soname(const std::string &soname);

} // namespace ghul_llvm
