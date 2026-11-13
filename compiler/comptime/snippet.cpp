#include "comptime/snippet.hpp"

#include <utility>

#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

namespace l::comptime {

std::vector<ast::StmtPtr> parseSnippet(const std::string& source,
                                       const ast::SourceLocation& loc,
                                       diag::DiagnosticEngine& diags) {
  if (source.empty()) {
    return {};
  }
  const std::string filename =
      loc.filename.empty() ? std::string("<lcmd-inject>") : loc.filename + ":lcmd";
  lexer::Lexer lexer(filename, source);
  // Snippet parse must not re-enter class hooks (injected decls are plain AST).
  parser::Parser parser(lexer.tokenize(), &diags);
  parser.setClassHookEnabled(false);
  std::vector<ast::StmtPtr> out;
  while (!parser.isAtEnd()) {
    ast::StmtPtr stmt = parser.parseStatement();
    if (stmt) {
      if (stmt->loc.filename.empty() || stmt->loc.line == 0) {
        stmt->loc = loc;
      }
      out.push_back(std::move(stmt));
      continue;
    }
    break;
  }
  return out;
}

}  // namespace l::comptime
