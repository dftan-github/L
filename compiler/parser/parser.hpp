#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ast/ast.hpp"
#include "ast/token.hpp"

namespace l::parser {

class Parser {
public:
  explicit Parser(std::vector<ast::Token> tokens);

  std::unique_ptr<ast::Module> parseModule();
  ast::StmtPtr parseStatement();
  ast::ExprPtr parseExpression(int min_precedence = 0);

  bool hadError() const { return had_error_; }
  const std::vector<std::string>& diagnostics() const { return diagnostics_; }

private:
  const ast::Token& peek(std::size_t offset = 0) const;
  bool check(ast::TokenType type) const;
  bool match(ast::TokenType type);
  ast::Token consume(ast::TokenType type, const char* message);
  void advance();
  bool atEnd() const;

  void errorAt(const ast::Token& tok, const std::string& message);
  void synchronize();

  static bool isTypeToken(ast::TokenType type);
  static std::string typeTokenLexeme(const ast::Token& tok);
  static int infixPrecedence(ast::TokenType type);

  ast::ExprPtr parsePrimaryExpression();
  ast::ExprPtr parseCallArgs(std::string callee, ast::SourceLocation loc);

  ast::StmtPtr parseConstdDecl();
  ast::StmtPtr parseExplicitTypeDecl();
  ast::StmtPtr parseInferDecl();
  ast::StmtPtr parseAssignStmt();
  ast::StmtPtr parseFuncDecl();
  ast::StmtPtr parseReturnStmt();
  ast::StmtPtr parseExprStmt();
  ast::StmtPtr parseIfStmt();
  ast::StmtPtr parseWhileStmt();
  ast::Param parseParam();
  std::vector<ast::StmtPtr> parseBlockBody();

  std::vector<ast::Token> tokens_;
  std::size_t index_ = 0;
  bool had_error_ = false;
  std::vector<std::string> diagnostics_;
};

}  // namespace l::parser
