#pragma once

#include <string>

namespace llvm {
class Module;
}

namespace l::jit {

// Run `main` from an LLVM IR module via ORC LLJIT (no object / exe on disk).
// Resolves L runtime symbols that are linked into the host process.
// Returns process exit code from main(), or -1 on JIT failure (message on stderr).
int runModuleMain(llvm::Module& module);

}  // namespace l::jit
