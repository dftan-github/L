#pragma once

#include <memory>

#include "ast/ast.hpp"
#include "ast/token.hpp"
#include "lexer/lexer.hpp"

namespace l::parser {

class Parser {
public:
  explicit Parser(lexer::Lexer& lexer);

  // Panic-mode recovery: collect diagnostics; do not throw.
  // Phase 1: stub only — Parser not implemented yet.
  std::unique_ptr<ast::Module> parseModule();

private:
  lexer::Lexer& lexer_;
  ast::Token current_;
};

}  // namespace l::parser
