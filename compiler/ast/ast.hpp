#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "ast/token.hpp"

// AST nodes: owned via std::unique_ptr only. Dispatch via kind() (no RTTI).

namespace l::ast {

enum class ExprKind {
  IntLiteral,
  BoolLiteral,
  Identifier,
  Binary,
  Call,
};

enum class StmtKind {
  VarDecl,
  Assign,
  Return,
  FuncDecl,
  ExprStmt,
  If,
  While,
};

struct Node {
  virtual ~Node() = default;
  SourceLocation loc;
  virtual void dump(std::ostream& out, int indent = 0) const = 0;
};

using NodePtr = std::unique_ptr<Node>;

struct Expr : Node {
  virtual ExprKind kind() const = 0;
};
using ExprPtr = std::unique_ptr<Expr>;

struct Stmt : Node {
  virtual StmtKind kind() const = 0;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct IntLiteralExpr : Expr {
  std::string value;
  ExprKind kind() const override { return ExprKind::IntLiteral; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct BooleanLiteralExpr : Expr {
  bool value = false;
  ExprKind kind() const override { return ExprKind::BoolLiteral; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct IdentifierExpr : Expr {
  std::string name;
  ExprKind kind() const override { return ExprKind::Identifier; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct BinaryExpr : Expr {
  std::string op;
  ExprPtr left;
  ExprPtr right;
  ExprKind kind() const override { return ExprKind::Binary; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct CallExpr : Expr {
  std::string callee;
  std::vector<ExprPtr> args;
  ExprKind kind() const override { return ExprKind::Call; }
  void dump(std::ostream& out, int indent = 0) const override;
};

enum class VarDeclKind { Constd, ExplicitType, Infer };

struct VarDeclStmt : Stmt {
  VarDeclKind decl_kind = VarDeclKind::Infer;
  std::string type_name;
  std::string name;
  ExprPtr init;
  StmtKind kind() const override { return StmtKind::VarDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct AssignStmt : Stmt {
  std::string name;
  ExprPtr value;
  StmtKind kind() const override { return StmtKind::Assign; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct Param {
  std::string type_name;
  std::string name;
  SourceLocation loc;
  void dump(std::ostream& out, int indent = 0) const;
};

struct ReturnStmt : Stmt {
  ExprPtr value;
  StmtKind kind() const override { return StmtKind::Return; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct FuncDeclStmt : Stmt {
  std::string name;
  std::string return_type;
  std::vector<Param> params;
  std::vector<StmtPtr> body;
  StmtKind kind() const override { return StmtKind::FuncDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct ExprStmt : Stmt {
  ExprPtr expr;
  StmtKind kind() const override { return StmtKind::ExprStmt; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct IfStmt : Stmt {
  ExprPtr cond;
  std::vector<StmtPtr> then_body;
  std::vector<StmtPtr> else_body;  // empty if no else
  StmtKind kind() const override { return StmtKind::If; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct WhileStmt : Stmt {
  ExprPtr cond;
  std::vector<StmtPtr> body;
  StmtKind kind() const override { return StmtKind::While; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct Module {
  std::vector<StmtPtr> stmts;
  void dump(std::ostream& out, int indent = 0) const;
};

}  // namespace l::ast
