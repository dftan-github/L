#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ast/ast.hpp"
#include "ast/token.hpp"
#include "diagnostic/diagnostic.hpp"

namespace l::parser {

// Fired when a DirectiveStmt (//LCMD_...) is constructed.
using AstHook = std::function<void(ast::DirectiveStmt&)>;

class Parser {
public:
  explicit Parser(std::vector<ast::Token> tokens, diag::DiagnosticEngine* diags = nullptr);

  std::unique_ptr<ast::Module> parseModule();
  ast::StmtPtr parseStatement();
  ast::ExprPtr parseExpression(int min_precedence = 0);

  void registerClassName(const std::string& name);
  void registerInterfaceName(const std::string& name);
  void addAstHook(AstHook hook);

  // Opt-in ClassDecl hooks ([hook-performance]). Default false = zero interpreter cost.
  void setClassHookEnabled(bool enabled) { class_hook_enabled_ = enabled; }
  bool classHookEnabled() const { return class_hook_enabled_; }

  bool hadError() const { return had_error_; }
  bool isAtEnd() const { return atEnd(); }
  // True when only optional trailing `;` remain (ASI after an interp expression).
  bool onlyTrailingSemicolonsLeft() const;

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
  // `"text {expr}"` → BinaryExpr chain of `+` (rule: string interpolation).
  ast::ExprPtr parseStringLiteralOrInterp();
  ast::ExprPtr parseInterpFragment(const std::string& src, const ast::SourceLocation& loc);
  static std::string unescapeStringFragment(const std::string& raw);
  ast::ExprPtr parseUnaryExpression();
  ast::ExprPtr parsePostfix(ast::ExprPtr left);
  ast::ExprPtr parseCallArgs(std::string callee, ast::SourceLocation loc);
  std::vector<ast::CallArg> parseArgList();
  // `{ |x| ... }` trailing / anonymous function.
  std::unique_ptr<ast::LambdaExpr> parseTrailingBlock();
  // Standalone `|x| expr` / `|x| { ... }` (Phase 1: no captures).
  ast::ExprPtr parseLambdaExpr();
  void parseLambdaParamList(ast::LambdaExpr& lambda);
  ast::ExprPtr parseArrayLiteral();
  // After `foo(...)`, bare `{ ... }` is a zero-arg trailing block; after `obj.m`,
  // require `{ |` so `obj.field { .x = 1 }` ([config-syntax]) is not treated as a lambda.
  void attachTrailingBlockIfPresent(std::vector<ast::CallArg>& args,
                                    bool allow_bare_brace = false);
  bool maybeConsumeStmtSemicolon();

  ast::StmtPtr parseConstDecl();
  ast::StmtPtr parseConstdDecl();
  ast::StmtPtr parseExplicitTypeDecl();
  ast::StmtPtr parseInferDecl();
  bool looksLikeDestructureDecl() const;
  ast::StmtPtr parseDestructureDecl();
  ast::StmtPtr parseAssignOrExprStmt();
  // `base { .field = expr ... }` → BlockStmt of AssignStmts (property sugar intact).
  ast::StmtPtr parseRelativeAssignBlock(ast::ExprPtr base);
  // Deep-copy an lvalue expr for compound-assign desugar (`a += b` → `a = a + b`).
  static ast::ExprPtr cloneExpr(const ast::Expr& expr);
  ast::StmtPtr parseFuncDecl();
  std::unique_ptr<ast::FuncDeclStmt> parseFunCommon(bool allow_body);
  ast::StmtPtr parseReturnStmt();
  ast::StmtPtr parseExprStmt();
  ast::StmtPtr parseIfStmt();
  ast::StmtPtr parseMatchStmt();
  ast::StmtPtr parseWhileStmt();
  ast::StmtPtr parseForInStmt();
  ast::MatchBranch parseMatchBranch();
  ast::StmtPtr parseClassDecl();
  ast::StmtPtr parseInterfaceDecl();
  ast::StmtPtr parseDeferStmt();
  ast::StmtPtr parseSyncBlock();
  ast::StmtPtr parseSpawnBlock();
  ast::StmtPtr parseComptimeBlock();
  ast::StmtPtr parseImportStmt();
  ast::StmtPtr parseExternModule();
  ast::StmtPtr parsePubDecl();
  ast::StmtPtr parseDirective();
  ast::Param parseParam();
  std::vector<ast::StmtPtr> parseBlockBody();

  void noteParsedStmt(ast::Stmt& stmt);
  void flushPendingInjections(ast::Module& module);
  void maybeRunClassHook(ast::ClassDeclStmt& cls);
  void pushContext(const std::string& name);
  void popContext();
  std::string currentContextPath() const;

  std::vector<ast::Token> tokens_;
  std::size_t index_ = 0;
  bool had_error_ = false;
  diag::DiagnosticEngine* diags_ = nullptr;
  std::unordered_set<std::string> class_names_;
  std::unordered_set<std::string> interface_names_;
  std::unordered_set<std::string> generic_type_names_;  // in-scope <T: ...>
  std::vector<AstHook> ast_hooks_;
  // [hook-performance]: ClassDecl hooks are off unless //LCMD_enable_class_hook().
  bool class_hook_enabled_ = false;
  // Hierarchical path stack: class / fun names → e.g. User.generate
  std::vector<std::string> context_path_;
  // Top-level funcs seen so far (for __on_class_parsed lookup during parse).
  std::unordered_map<std::string, ast::FuncDeclStmt*> comptime_funcs_;
  std::vector<ast::StmtPtr> pending_injections_;
  // Relative-init bases (`obj { .x = ... }`); leading `.name` binds to the top.
  std::vector<ast::ExprPtr> relative_bases_;
};

}  // namespace l::parser
