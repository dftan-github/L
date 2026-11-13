#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ast/ast.hpp"
#include "diagnostic/diagnostic.hpp"

namespace l::module {

// Extra directories searched when resolving `import name` / `import "x.l"`.
using ImportSearchPaths = std::vector<std::filesystem::path>;

// Recursively parse `import "x.l"` / `import x`, merge ASTs, tag module_name.
// Parse errors are recorded in `diags`; a partial AST is still returned when possible.
// Returns nullptr only on hard I/O / cycle failures that leave no usable module.
std::unique_ptr<ast::Module> loadEntry(const std::string& entry_path,
                                       diag::DiagnosticEngine& diags,
                                       const ImportSearchPaths& search_paths = {});

// Multi-file CLI: load each entry (with its imports) and concatenate.
std::unique_ptr<ast::Module> loadEntries(const std::vector<std::string>& entry_paths,
                                         diag::DiagnosticEngine& diags,
                                         const ImportSearchPaths& search_paths = {});

// In-memory source buffer with a virtual file name for SourceLocation / errors.
// Does not write the buffer to disk. Imports still resolve from the process CWD.
std::unique_ptr<ast::Module> loadFromString(const std::string& source,
                                            const std::string& virtual_filename,
                                            diag::DiagnosticEngine& diags,
                                            const ImportSearchPaths& search_paths = {});

// Mixed CLI inputs: disk paths and/or memory buffers (name + source).
struct SourceUnit {
  bool from_memory = false;
  std::string name;    // file path, or virtual name when from_memory
  std::string source;  // required when from_memory
};

std::unique_ptr<ast::Module> loadUnits(const std::vector<SourceUnit>& units,
                                       diag::DiagnosticEngine& diags,
                                       const ImportSearchPaths& search_paths = {});

}  // namespace l::module
