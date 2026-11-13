#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ast/ast.hpp"
#include "diagnostic/diagnostic.hpp"
#include "module/loader.hpp"

namespace l::compile {

// Parse + LCMD-expand an in-memory source buffer.
// All Token/AST SourceLocation.filename values use `virtualFileName`
// (e.g. "<stdin>", "<eval>"). Never writes the buffer to a temp file.
std::unique_ptr<ast::Module> compileString(const std::string& source,
                                           const std::string& virtualFileName,
                                           diag::DiagnosticEngine& diags);

// Load/parse each unit (files and/or memory buffers), merge, then LCMD-expand.
std::unique_ptr<ast::Module> compileInputs(const std::vector<module::SourceUnit>& inputs,
                                           diag::DiagnosticEngine& diags,
                                           const module::ImportSearchPaths& search_paths = {});

}  // namespace l::compile
