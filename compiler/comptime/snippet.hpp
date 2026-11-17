#pragma once

#include <string>
#include <vector>

#include "ast/ast.hpp"
#include "diagnostic/diagnostic.hpp"

namespace l::comptime {

// Lex + parse a frontend-injected L source fragment into statements.
// `class_names` / `interface_names` are the enclosing parse's known type names:
// a fresh Parser starts with an empty table, so without them a fragment like
// `delegate Engine` or `Engine e` cannot tell a type from an identifier.
std::vector<ast::StmtPtr> parseSnippet(const std::string& source,
                                       const ast::SourceLocation& loc,
                                       diag::DiagnosticEngine& diags,
                                       const std::vector<std::string>& class_names = {},
                                       const std::vector<std::string>& interface_names = {});

// Same, but parsed with CLASS-BODY semantics: `i32 count` / `pub i32 count`
// become field declarations and plain `fun` becomes a method. Used when a
// directive is injected into a class (`//LCMD_` in the class body), where a
// bare `pub i32 x` is not a top-level `pub` declaration.
std::vector<ast::StmtPtr> parseClassSnippet(const std::string& source,
                                            const ast::SourceLocation& loc,
                                            diag::DiagnosticEngine& diags,
                                            const std::vector<std::string>& class_names = {},
                                            const std::vector<std::string>& interface_names = {});

}  // namespace l::comptime
