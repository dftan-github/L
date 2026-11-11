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
  StringLiteral,
  Identifier,
  Binary,
  Call,
  MemberAccess,
  MethodCall,
  New,
  Heap,
};

enum class StmtKind {
  VarDecl,
  Assign,
  Return,
  FuncDecl,
  ExprStmt,
  If,
  While,
  ClassDecl,
  FieldDecl,
  InterfaceDecl,
  Defer,
  Sync,
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

struct StringLiteralExpr : Expr {
  std::string value;
  ExprKind kind() const override { return ExprKind::StringLiteral; }
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

struct MemberAccessExpr : Expr {
  ExprPtr object;
  std::string member;
  ExprKind kind() const override { return ExprKind::MemberAccess; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct MethodCallExpr : Expr {
  ExprPtr object;
  std::string method;
  std::vector<ExprPtr> args;
  ExprKind kind() const override { return ExprKind::MethodCall; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct NewExpr : Expr {
  std::string class_name;
  std::vector<ExprPtr> args;
  ExprKind kind() const override { return ExprKind::New; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct HeapExpr : Expr {
  ExprPtr inner;  // typically NewExpr
  ExprKind kind() const override { return ExprKind::Heap; }
  void dump(std::ostream& out, int indent = 0) const override;
};

enum class VarDeclKind { Constd, ExplicitType, Infer };

struct VarDeclStmt : Stmt {
  VarDeclKind decl_kind = VarDeclKind::Infer;
  std::string type_name;
  std::string name;
  bool is_mutable = false;  // declared with trailing '!'
  ExprPtr init;
  StmtKind kind() const override { return StmtKind::VarDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct AssignStmt : Stmt {
  ExprPtr target;  // IdentifierExpr or MemberAccessExpr
  ExprPtr value;
  bool mutable_marker = false;  // LHS used '!'
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

struct GenericParam {
  std::string name;        // e.g. T
  std::string constraint;  // e.g. IReader  (required: <T: Interface>)
  SourceLocation loc;
};

struct FuncDeclStmt : Stmt {
  std::string name;
  std::string return_type;
  std::vector<GenericParam> generics;  // empty => non-generic
  std::vector<Param> params;
  std::vector<StmtPtr> body;  // empty for interface method signatures
  bool is_method = false;
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

struct FieldDeclStmt : Stmt {
  std::string type_name;
  std::string name;
  StmtKind kind() const override { return StmtKind::FieldDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct ClassDeclStmt : Stmt {
  std::string name;
  std::vector<std::string> interfaces;  // implemented interface names
  std::vector<std::unique_ptr<FieldDeclStmt>> fields;
  std::vector<std::unique_ptr<FuncDeclStmt>> methods;
  StmtKind kind() const override { return StmtKind::ClassDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct InterfaceDeclStmt : Stmt {
  std::string name;
  std::vector<std::unique_ptr<FuncDeclStmt>> methods;  // signatures only
  StmtKind kind() const override { return StmtKind::InterfaceDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct DeferStmt : Stmt {
  std::vector<StmtPtr> body;
  StmtKind kind() const override { return StmtKind::Defer; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct SyncBlockStmt : Stmt {
  std::vector<StmtPtr> body;
  StmtKind kind() const override { return StmtKind::Sync; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct Module {
  std::vector<StmtPtr> stmts;
  void dump(std::ostream& out, int indent = 0) const;
};

}  // namespace l::ast
