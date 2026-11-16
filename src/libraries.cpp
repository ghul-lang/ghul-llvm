#include "libraries.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace ghul_llvm {

namespace {

#if defined(__x86_64__)
constexpr uint16_t machine = llvm::ELF::EM_X86_64;
constexpr const char *multiarch = "x86_64-linux-gnu";
// The architecture bits of an ld.so.cache entry's flags: FLAG_X8664_LIB64.
constexpr int32_t cache_architecture = 0x0300;
#elif defined(__aarch64__)
constexpr uint16_t machine = llvm::ELF::EM_AARCH64;
constexpr const char *multiarch = "aarch64-linux-gnu";
// FLAG_AARCH64_LIB64.
constexpr int32_t cache_architecture = 0x0a00;
#else
#error "no dynamic linking for this architecture"
#endif

// The directories the dynamic loader searches when its cache does not name
// a library, most specific first.
std::vector<std::string> standard_directories() {
    return {std::string("/lib/") + multiarch, std::string("/usr/lib/") + multiarch,
            "/lib64", "/usr/lib64", "/lib", "/usr/lib", "/usr/local/lib"};
}

// What a shared object says of itself: whether it is one for this machine,
// and its soname.
struct Described {
    bool usable = false;
    std::string soname;
};

Described describe(const std::string &path) {
    Described result;

    auto buffer = llvm::MemoryBuffer::getFile(path, /*IsText=*/false,
                                              /*RequiresNullTerminator=*/false);

    if (!buffer) {
        return result;
    }

    auto file = llvm::object::ELF64LEFile::create((*buffer)->getBuffer());

    if (!file) {
        llvm::consumeError(file.takeError());
        return result;
    }

    if (file->getHeader().e_machine != machine || file->getHeader().e_type != llvm::ELF::ET_DYN) {
        return result;
    }

    result.usable = true;

    auto entries = file->dynamicEntries();

    if (!entries) {
        llvm::consumeError(entries.takeError());
        return result;
    }

    uint64_t strings = 0;
    std::optional<uint64_t> soname;

    for (const auto &entry : *entries) {
        if (entry.d_tag == llvm::ELF::DT_STRTAB) {
            strings = entry.d_un.d_ptr;
        } else if (entry.d_tag == llvm::ELF::DT_SONAME) {
            soname = entry.d_un.d_val;
        }
    }

    if (!soname || !strings) {
        return result;
    }

    auto table = file->toMappedAddr(strings);

    if (!table) {
        llvm::consumeError(table.takeError());
        return result;
    }

    const char *start = reinterpret_cast<const char *>(*table) + *soname;
    const char *end = (*buffer)->getBufferEnd();

    if (start < (*buffer)->getBufferStart() || start >= end) {
        return result;
    }

    result.soname = std::string(start, strnlen(start, end - start));
    return result;
}

// The numbers after ".so." in a file name, as a list to compare: libz.so.1.3.1
// gives 1, 3, 1, and a name with none gives an empty list.
std::vector<unsigned long> numbers_after_so(llvm::StringRef name) {
    std::vector<unsigned long> numbers;
    size_t at = name.find(".so.");

    if (at == llvm::StringRef::npos) {
        return numbers;
    }

    llvm::SmallVector<llvm::StringRef, 4> parts;
    name.substr(at + 4).split(parts, '.');

    for (auto part : parts) {
        unsigned long value;

        if (part.getAsInteger(10, value)) {
            break;
        }

        numbers.push_back(value);
    }

    return numbers;
}

// The version a library's real file name carries, following symbolic links.
std::string version_of(const std::string &path) {
    llvm::SmallString<256> real;

    if (llvm::sys::fs::real_path(path, real)) {
        return "";
    }

    llvm::StringRef name = llvm::sys::path::filename(real);
    size_t at = name.find(".so.");

    return at == llvm::StringRef::npos ? "" : name.substr(at + 4).str();
}

// The library `path` is, if it is one for this machine: linked through the
// file its soname names where that is beside it.
std::optional<SharedLibrary> library_at(const std::string &path) {
    Described described = describe(path);

    if (!described.usable) {
        return std::nullopt;
    }

    SharedLibrary library;
    library.path = path;
    library.soname = described.soname;

    if (!described.soname.empty()) {
        llvm::SmallString<256> beside(llvm::sys::path::parent_path(path));
        llvm::sys::path::append(beside, described.soname);

        if (llvm::sys::fs::exists(beside)) {
            library.path = std::string(beside);
        }
    }

    library.version = version_of(library.path);
    return library;
}

// The best of the libraries named lib<name>.so or lib<name>.so.<numbers> in
// `directory`: the one whose soname has the highest numbers.
std::optional<SharedLibrary> find_in_directory(const std::string &name, const std::string &directory) {
    std::string stem = "lib" + name + ".so";
    std::optional<SharedLibrary> best;
    std::vector<unsigned long> best_numbers;

    std::error_code error;

    for (llvm::sys::fs::directory_iterator entry(directory, error), end; entry != end && !error;
         entry.increment(error)) {
        llvm::StringRef file = llvm::sys::path::filename(entry->path());

        if (file != stem && !(file.starts_with(stem + ".") && !numbers_after_so(file).empty())) {
            continue;
        }

        auto library = library_at(entry->path());

        if (!library) {
            continue;
        }

        auto numbers = numbers_after_so(library->soname.empty() ? file : llvm::StringRef(library->soname));

        if (!best || numbers > best_numbers) {
            best = library;
            best_numbers = numbers;
        }
    }

    return best;
}

// An entry of the dynamic loader's cache: a soname and the file it is.
struct CacheEntry {
    std::string soname;
    std::string path;
};

// Reads /etc/ld.so.cache in glibc's format, glibc-ld.so.cache1.1, the only
// one written since glibc 2.37 and the default since 2.32, keeping the
// entries for this architecture's C library. A missing or unreadable cache
// gives none.
std::vector<CacheEntry> read_cache() {
    std::vector<CacheEntry> entries;

    auto buffer = llvm::MemoryBuffer::getFile("/etc/ld.so.cache", /*IsText=*/false,
                                              /*RequiresNullTerminator=*/false);

    if (!buffer) {
        return entries;
    }

    llvm::StringRef data = (*buffer)->getBuffer();
    const llvm::StringRef magic = "glibc-ld.so.cache1.1";
    size_t start = data.find(magic);

    // The header: the magic, the entry count, the string table's length,
    // a flags byte and three of padding, the extension offset and three
    // unused words, then the entries, each two flags-and-offsets words, an
    // unused word and the hardware capabilities.
    const size_t header = 20 + 4 + 4 + 4 + 4 + 12;
    const size_t entry_size = 4 + 4 + 4 + 4 + 8;

    if (start == llvm::StringRef::npos || data.size() < start + header) {
        return entries;
    }

    auto word = [&](size_t at) {
        uint32_t value;
        std::memcpy(&value, data.data() + at, sizeof value);
        return value;
    };

    uint32_t count = word(start + 20);

    if (data.size() < start + header + uint64_t(count) * entry_size) {
        return entries;
    }

    // Offsets of strings are from the start of the new format's header.
    auto string_at = [&](uint32_t offset) -> std::optional<std::string> {
        size_t at = start + offset;

        if (at >= data.size()) {
            return std::nullopt;
        }

        const char *text = data.data() + at;
        return std::string(text, strnlen(text, data.size() - at));
    };

    for (uint32_t i = 0; i < count; ++i) {
        size_t at = start + header + size_t(i) * entry_size;
        int32_t flags = static_cast<int32_t>(word(at));

        // FLAG_ELF_LIBC6 in the type bits, and this architecture.
        if ((flags & 0x00ff) != 0x0003 || (flags & 0xff00) != cache_architecture) {
            continue;
        }

        auto soname = string_at(word(at + 4));
        auto path = string_at(word(at + 8));

        if (soname && path) {
            entries.push_back({*soname, *path});
        }
    }

    return entries;
}

const std::vector<CacheEntry> &cache() {
    static const std::vector<CacheEntry> entries = read_cache();
    return entries;
}

// The best library in the cache whose soname starts lib<name>.so.
std::optional<SharedLibrary> find_in_cache(const std::string &name) {
    std::string stem = "lib" + name + ".so";
    std::optional<SharedLibrary> best;
    std::vector<unsigned long> best_numbers;

    for (const auto &entry : cache()) {
        llvm::StringRef soname = entry.soname;

        if (soname != stem && !soname.starts_with(stem + ".")) {
            continue;
        }

        auto numbers = numbers_after_so(soname);

        if (best && !(numbers > best_numbers)) {
            continue;
        }

        if (auto library = library_at(entry.path)) {
            best = library;
            best_numbers = numbers;
        }
    }

    return best;
}

} // namespace

std::optional<SharedLibrary> find_library(const std::string &name,
                                          const std::vector<std::string> &library_paths) {
    for (const auto &directory : library_paths) {
        if (auto library = find_in_directory(name, directory)) {
            library->from_library_path = true;
            return library;
        }
    }

    if (auto library = find_in_cache(name)) {
        return library;
    }

    for (const auto &directory : standard_directories()) {
        if (auto library = find_in_directory(name, directory)) {
            return library;
        }
    }

    return std::nullopt;
}

std::optional<SharedLibrary> find_soname(const std::string &soname) {
    for (const auto &entry : cache()) {
        if (entry.soname == soname) {
            if (auto library = library_at(entry.path)) {
                return library;
            }
        }
    }

    for (const auto &directory : standard_directories()) {
        llvm::SmallString<256> path(directory);
        llvm::sys::path::append(path, soname);

        if (llvm::sys::fs::exists(path)) {
            if (auto library = library_at(std::string(path))) {
                return library;
            }
        }
    }

    return std::nullopt;
}

} // namespace ghul_llvm
