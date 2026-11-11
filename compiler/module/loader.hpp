#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ast/ast.hpp"

namespace l::module {

// Recursively parse `import "x.l"` / `import x`, merge ASTs, tag module_name.
// Returns nullptr on error (diagnostics printed to stderr).
std::unique_ptr<ast::Module> loadEntry(const std::string& entry_path);

// Multi-file CLI: load each entry (with its imports) and concatenate.
std::unique_ptr<ast::Module> loadEntries(const std::vector<std::string>& entry_paths);

}  // namespace l::module
