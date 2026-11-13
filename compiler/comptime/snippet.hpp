#pragma once

#include <string>
#include <vector>

#include "ast/ast.hpp"
#include "diagnostic/diagnostic.hpp"

namespace l::comptime {

// Lex + parse a frontend-injected L source fragment into statements.
std::vector<ast::StmtPtr> parseSnippet(const std::string& source,
                                       const ast::SourceLocation& loc,
                                       diag::DiagnosticEngine& diags);

}  // namespace l::comptime
