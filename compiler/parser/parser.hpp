#pragma once

#include <memory>
#include <string>
#include <unordered_set>
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

  void registerClassName(const std::string& name);
  void registerInterfaceName(const std::string& name);

  bool hadError() const { return had_error_; }
  const std::vector<std::string>& diagnostics() const { return diagnostics_; }

private:
  bool parseGenericParams(std::vector<ast::GenericParam>& out, bool allow_naked);
  const ast::Token& peek(std::size_t offset = 0) const;
  bool check(ast::TokenType type) const;
  bool match(ast::TokenType type);
  ast::Token consume(ast::TokenType type, const char* message);
  void advance();
  bool atEnd() const;

  void errorAt(const ast::Token& tok, const std::string& message);
  void synchronize();

  static bool isBuiltinTypeToken(ast::TokenType type);
  bool isTypeStart() const;
  bool looksLikeTypedDecl() const;
  std::size_t skipGenericArgs(std::size_t from) const;
  std::string parseTypeName();
  std::string parseTypeArgsSuffix(std::string base);
  static std::string typeTokenLexeme(const ast::Token& tok);
  static int infixPrecedence(ast::TokenType type);

  ast::ExprPtr parsePrimaryExpression();
  ast::ExprPtr parsePostfix(ast::ExprPtr left);
  ast::ExprPtr parseCallArgs(std::string callee, ast::SourceLocation loc);
  std::vector<ast::CallArg> parseArgList();

  ast::StmtPtr parseConstdDecl();
  ast::StmtPtr parseExplicitTypeDecl();
  ast::StmtPtr parseInferDecl();
  ast::StmtPtr parseAssignOrExprStmt();
  ast::StmtPtr parseFuncDecl();
  std::unique_ptr<ast::FuncDeclStmt> parseFunCommon(bool allow_body);
  ast::StmtPtr parseReturnStmt();
  ast::StmtPtr parseExprStmt();
  ast::StmtPtr parseIfStmt();
  ast::StmtPtr parseWhileStmt();
  ast::StmtPtr parseForInStmt();
  ast::StmtPtr parseClassDecl();
  ast::StmtPtr parseInterfaceDecl();
  ast::StmtPtr parseDeferStmt();
  ast::StmtPtr parseSyncBlock();
  ast::StmtPtr parseImportStmt();
  ast::StmtPtr parsePubDecl();
  ast::Param parseParam();
  std::vector<ast::StmtPtr> parseBlockBody();

  std::vector<ast::Token> tokens_;
  std::size_t index_ = 0;
  bool had_error_ = false;
  std::vector<std::string> diagnostics_;
  std::unordered_set<std::string> class_names_;
  std::unordered_set<std::string> interface_names_;
  std::unordered_set<std::string> generic_type_names_;  // in-scope <T: ...>
};

}  // namespace l::parser
