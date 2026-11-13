#pragma once

#include "ast/ast.hpp"
#include "diagnostic/diagnostic.hpp"

namespace l::comptime {

// Walk the module: for each //LCMD_ DirectiveStmt, call
// `pub fun __compile_time_handler(String cmd, String args) -> String`,
// parse the returned source, and splice the resulting stmts in place.
bool expandLcmdDirectives(ast::Module& module, diag::DiagnosticEngine& diags);

}  // namespace l::comptime
