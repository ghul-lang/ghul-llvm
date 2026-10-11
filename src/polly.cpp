// LLVM's release is built with Polly linked into its tools, so the LTO code
// that LLD carries asks for Polly's pass plugin by name. ghul-llvm runs no
// LTO and no Polly, so the plugin it is handed registers nothing, and Polly
// stays out of the executable.

#include "llvm/Plugins/PassPlugin.h"

namespace {

void register_nothing(llvm::PassBuilder &) {}

} // namespace

llvm::PassPluginLibraryInfo getPollyPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "Polly (absent)", "", register_nothing};
}
