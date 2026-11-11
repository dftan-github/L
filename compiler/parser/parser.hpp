#pragma once

#include <memory>

#include "ast/ast.hpp"
#include "lexer/lexer.hpp"

namespace l::parser {

class Parser {
public:
  explicit Parser(lexer::Lexer& lexer);

  // Panic-mode recovery: collect diagnostics; do not throw.
  std::unique_ptr<ast::Module> parseModule();

private:
  lexer::Lexer& lexer_;
  lexer::Token current_;
};

}  // namespace l::parser
