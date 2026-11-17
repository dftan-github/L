#include "comptime/snippet.hpp"

#include <utility>

#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

namespace l::comptime {
namespace {

// A snippet Parser is standalone, so it starts with an empty type table: every
// known class/interface name from the enclosing parse must be re-registered,
// otherwise `delegate Engine` / `Engine e` read `Engine` as an identifier.
template <typename ParseFn>
std::vector<ast::StmtPtr> parseWithTypes(const std::string& source,
                                         const ast::SourceLocation& loc,
                                         diag::DiagnosticEngine& diags,
                                         const std::vector<std::string>& class_names,
                                         const std::vector<std::string>& interface_names,
                                         ParseFn parse) {
  if (source.empty()) {
    return {};
  }
  const std::string filename =
      loc.filename.empty() ? std::string("<lcmd-inject>") : loc.filename + ":lcmd";
  lexer::Lexer lexer(filename, source);
  // Snippet parse must not re-enter class hooks (injected decls are plain AST).
  parser::Parser parser(lexer.tokenize(), &diags);
  parser.setClassHookEnabled(false);
  for (const std::string& c : class_names) {
    parser.registerClassName(c);
  }
  for (const std::string& i : interface_names) {
    parser.registerInterfaceName(i);
  }
  return parse(parser);
}

}  // namespace

std::vector<ast::StmtPtr> parseSnippet(const std::string& source,
                                       const ast::SourceLocation& loc,
                                       diag::DiagnosticEngine& diags,
                                       const std::vector<std::string>& class_names,
                                       const std::vector<std::string>& interface_names) {
  return parseWithTypes(
      source, loc, diags, class_names, interface_names,
      [&loc](parser::Parser& parser) {
        std::vector<ast::StmtPtr> out;
        while (!parser.isAtEnd()) {
          ast::StmtPtr stmt = parser.parseStatement();
          if (!stmt) {
            break;
          }
          if (stmt->loc.filename.empty() || stmt->loc.line == 0) {
            stmt->loc = loc;
          }
          out.push_back(std::move(stmt));
        }
        return out;
      });
}

std::vector<ast::StmtPtr> parseClassSnippet(const std::string& source,
                                            const ast::SourceLocation& loc,
                                            diag::DiagnosticEngine& diags,
                                            const std::vector<std::string>& class_names,
                                            const std::vector<std::string>& interface_names) {
  // Class-body view: a member list terminated by EOF instead of a statement list.
  return parseWithTypes(source, loc, diags, class_names, interface_names,
                        [](parser::Parser& parser) { return parser.parseClassMembers(); });
}

}  // namespace l::comptime
