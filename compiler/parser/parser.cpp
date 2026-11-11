#include "parser/parser.hpp"

namespace l::parser {

Parser::Parser(lexer::Lexer& lexer) : lexer_(lexer), current_(lexer_.next()) {}

std::unique_ptr<ast::Module> Parser::parseModule() {
  auto module = std::make_unique<ast::Module>();
  // TODO: Parser — Phase 1 step 2. Lexer is ready; do not parse here yet.
  (void)current_;
  return module;
}

}  // namespace l::parser
