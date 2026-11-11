#include "parser/parser.hpp"

namespace l::parser {

Parser::Parser(lexer::Lexer& lexer) : lexer_(lexer), current_(lexer_.next()) {}

std::unique_ptr<ast::Module> Parser::parseModule() {
  auto module = std::make_unique<ast::Module>();
  // TODO: consume tokens into AST. Golden case: README §18 / tests/cases/golden.l
  return module;
}

}  // namespace l::parser
