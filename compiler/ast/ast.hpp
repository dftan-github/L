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
  FloatLiteral,
  BoolLiteral,
  StringLiteral,
  Identifier,
  Binary,
  Coalesce,  // left ?? right — Optional unwrap with fallback
  Call,
  MemberAccess,
  This,
  MethodCall,
  New,
  Heap,
  Index,
  Sizeof,
  ArrayLiteral,
  Lambda,
  Cast,
  IncDec,
  Try,  // expr? — Result error propagation
  Spawn,  // spawn { ... } → ptr thread handle
  AddrOf,  // &expr → ptr (address of lvalue)
  Match,   // match expr { some(v) => ..., none => ... } — value-producing
};

enum class StmtKind {
  VarDecl,
  DestructureDecl,
  Assign,
  Return,
  FuncDecl,
  ExprStmt,
  Block,  // desugared relative-init `obj { .x = ... }` → nested AssignStmts
  If,
  Match,
  While,
  ForIn,
  Break,
  Continue,
  ClassDecl,
  FieldDecl,
  InterfaceDecl,
  Defer,
  Sync,
  Spawn,
  Comptime,
  Import,
  ExternModule,
  Directive,
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

struct FloatLiteralExpr : Expr {
  std::string value;
  ExprKind kind() const override { return ExprKind::FloatLiteral; }
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

// `left ?? right` — if `left` (Optional<T>) is some, yield its value; otherwise
// evaluate and yield `right` (T). Desugared by Codegen into a presence branch.
struct CoalesceExpr : Expr {
  ExprPtr left;
  ExprPtr right;
  ExprKind kind() const override { return ExprKind::Coalesce; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// One call-site argument: positional (name empty) or keyword (`name: value`).
struct CallArg {
  std::string name;  // empty => positional
  ExprPtr value;
  SourceLocation loc;
  void dump(std::ostream& out, int indent = 0) const;
};

struct CallExpr : Expr {
  std::string callee;
  std::vector<CallArg> args;
  ExprKind kind() const override { return ExprKind::Call; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct MemberAccessExpr : Expr {
  ExprPtr object;
  std::string member;
  ExprKind kind() const override { return ExprKind::MemberAccess; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// `this` — the implicit receiver of an instance method.
struct ThisExpr : Expr {
  ExprKind kind() const override { return ExprKind::This; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct MethodCallExpr : Expr {
  ExprPtr object;
  std::string method;
  std::vector<CallArg> args;
  ExprKind kind() const override { return ExprKind::MethodCall; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct NewExpr : Expr {
  std::string class_name;
  std::vector<CallArg> args;
  ExprKind kind() const override { return ExprKind::New; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct HeapExpr : Expr {
  ExprPtr inner;  // typically NewExpr
  ExprKind kind() const override { return ExprKind::Heap; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct IndexExpr : Expr {
  ExprPtr object;
  ExprPtr index;
  ExprKind kind() const override { return ExprKind::Index; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct SizeofExpr : Expr {
  std::string type_name;
  ExprKind kind() const override { return ExprKind::Sizeof; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// `[1, 2, 3]` — codegen lowers to Array<T>() + push each element.
struct ArrayLiteralExpr : Expr {
  std::vector<ExprPtr> elements;
  ExprKind kind() const override { return ExprKind::ArrayLiteral; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// Lambda / trailing block: `|x| expr`, `|x| { ... }`, or `{ |x| ... }`.
struct LambdaExpr : Expr {
  std::vector<std::string> params;  // names only; types from call-site fun(T) or Phase-1 i32 default
  std::vector<StmtPtr> body;
  // Free locals referenced from the enclosing scope (filled by semantic).
  std::vector<std::string> captures;
  // True when this capturing lambda is returned from its enclosing function ([closure-escape]).
  bool escapes = false;
  ExprKind kind() const override { return ExprKind::Lambda; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// `val as Type` — class ptr cast, or interface→class downcast to Optional<Type>.
struct CastExpr : Expr {
  ExprPtr value;
  std::string type_name;
  ExprKind kind() const override { return ExprKind::Cast; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// `++a!` / `a!++` / `--a!` / `a!--` ([inc-dec]). Target is the lvalue without `!`.
struct IncDecExpr : Expr {
  bool is_prefix = true;
  bool is_inc = true;  // false → decrement
  ExprPtr target;
  ExprKind kind() const override { return ExprKind::IncDec; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// `expr?` — if Result is Err, early-return that error from the enclosing Result function.
struct TryExpr : Expr {
  ExprPtr inner;
  ExprKind kind() const override { return ExprKind::Try; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// `spawn { ... }` as expression — starts an OS thread and yields an opaque join handle (`ptr`).
struct SpawnExpr : Expr {
  std::vector<StmtPtr> body;
  ExprKind kind() const override { return ExprKind::Spawn; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// `&expr` — address of a mutable/local/global storage slot (yields `ptr`).
struct AddrOfExpr : Expr {
  ExprPtr operand;
  ExprKind kind() const override { return ExprKind::AddrOf; }
  void dump(std::ostream& out, int indent = 0) const override;
};

enum class VarDeclKind { Const, Constd, ExplicitType, Infer };

struct VarDeclStmt : Stmt {
  VarDeclKind decl_kind = VarDeclKind::Infer;
  std::string type_name;
  std::string name;
  bool is_mutable = false;  // declared with trailing '!'
  ExprPtr init;
  StmtKind kind() const override { return StmtKind::VarDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// One LHS name in `(a, b!) := expr` ([destructure]).
struct DestructureBinding {
  std::string name;
  bool is_mutable = false;
  SourceLocation loc;
};

// `(a, b) := class_or_struct_value` — bind fields in declaration order.
struct DestructureDeclStmt : Stmt {
  std::vector<DestructureBinding> bindings;
  ExprPtr init;
  StmtKind kind() const override { return StmtKind::DestructureDecl; }
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
  bool is_mutable = false;  // trailing '!' → mutable class reference
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
  std::vector<StmtPtr> body;  // empty for interface / extern signatures
  bool is_method = false;
  bool is_static = false;  // class static method: no implicit this
  bool is_pub = false;
  bool is_extern = false;  // C ABI: ExternalLinkage, unmangled name
  // Compiler-injected methods (e.g. auto `clone`) — body emitted by codegen, not AST.
  bool is_compiler_generated = false;
  // True when a capturing lambda is returned — LLVM ret is fat closure {fn,ctx} ([closure-escape]).
  bool returns_fat_closure = false;
  std::string module_name;  // file stem; used for private mangling
  StmtKind kind() const override { return StmtKind::FuncDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct ExprStmt : Stmt {
  ExprPtr expr;
  StmtKind kind() const override { return StmtKind::ExprStmt; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// Statement group (relative-init desugar: `app.rs232 { .baud = 115200 }` → Assigns).
struct BlockStmt : Stmt {
  std::vector<StmtPtr> stmts;
  StmtKind kind() const override { return StmtKind::Block; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct IfStmt : Stmt {
  ExprPtr cond;
  std::vector<StmtPtr> then_body;
  std::vector<StmtPtr> else_body;  // empty if no else
  StmtKind kind() const override { return StmtKind::If; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// Pattern shape for one `match` arm.
enum class MatchPatternKind {
  Literal,  // 1 / true / "s"
  Wildcard,  // _
  Some,      // some(val) — Optional present
  None,      // none      — Optional absent
};

// One arm of `match`, in either statement or expression form:
//   `1 => { ... }` / `_ => { ... }`          (statement)
//   `some(val) => expr` / `none => expr`     (expression)
struct MatchBranch {
  MatchPatternKind pattern_kind = MatchPatternKind::Literal;
  bool is_wildcard = false;  // legacy alias for pattern_kind == Wildcard
  ExprPtr pattern;  // Int / String / Bool literal when Literal
  std::string binding;  // `some(val)` binds the payload to this name
  std::vector<StmtPtr> body;  // statement form
  SourceLocation loc;
  ExprPtr value;  // expression form: arm yields this
};

struct MatchStmt : Stmt {
  ExprPtr scrutinee;
  std::vector<MatchBranch> branches;
  StmtKind kind() const override { return StmtKind::Match; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// Value-producing `match`:
//   x := match opt { some(v) => v.len(), none => 0 }
// Branch values are merged with a PHI node.
struct MatchExpr : Expr {
  ExprPtr scrutinee;
  std::vector<MatchBranch> branches;
  ExprKind kind() const override { return ExprKind::Match; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct WhileStmt : Stmt {
  ExprPtr cond;
  std::vector<StmtPtr> body;
  StmtKind kind() const override { return StmtKind::While; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// `break` / `continue` — only valid inside a loop body.
struct BreakStmt : Stmt {
  StmtKind kind() const override { return StmtKind::Break; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct ContinueStmt : Stmt {
  StmtKind kind() const override { return StmtKind::Continue; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct ForInStmt : Stmt {
  std::string item_name;  // loop variable
  ExprPtr iterable;       // typically IdentifierExpr naming an Array<T>
  std::vector<StmtPtr> body;
  StmtKind kind() const override { return StmtKind::ForIn; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct FieldDeclStmt : Stmt {
  std::string type_name;
  std::string name;
  bool is_pub = false;  // `pub i32 level_` — required for property fallback `u.level`
  bool is_delegate = false;  // `delegate Engine` — anonymous field; methods promote ([delegate])
  StmtKind kind() const override { return StmtKind::FieldDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct ClassDeclStmt : Stmt {
  std::string name;
  std::vector<GenericParam> generics;  // empty => non-generic; naked <T> allowed for containers
  std::vector<std::string> interfaces;  // implemented interface names
  std::vector<std::unique_ptr<FieldDeclStmt>> fields;
  std::vector<std::unique_ptr<FuncDeclStmt>> methods;
  bool is_pub = false;
  bool is_partial = false;  // `partial class` — merged in semantic before codegen
  std::string module_name;
  StmtKind kind() const override { return StmtKind::ClassDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct InterfaceDeclStmt : Stmt {
  std::string name;
  std::vector<std::unique_ptr<FuncDeclStmt>> methods;  // signatures only
  bool is_pub = false;
  std::string module_name;
  StmtKind kind() const override { return StmtKind::InterfaceDecl; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct ImportStmt : Stmt {
  std::string path;  // e.g. "mod_a.l"
  StmtKind kind() const override { return StmtKind::Import; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// `extern module name { fun f(...) -> T; ... }` — C ABI imports by exact symbol name.
struct ExternModuleStmt : Stmt {
  std::string name;
  std::vector<std::unique_ptr<FuncDeclStmt>> decls;  // each is_extern = true
  StmtKind kind() const override { return StmtKind::ExternModule; }
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

// Concurrent region: `!` writes here require `sync` (data-race check).
struct SpawnBlockStmt : Stmt {
  std::vector<StmtPtr> body;
  StmtKind kind() const override { return StmtKind::Spawn; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// Frontend-only: `comptime { ... }` — executed then stripped (never LLVM).
struct ComptimeStmt : Stmt {
  std::vector<StmtPtr> body;
  StmtKind kind() const override { return StmtKind::Comptime; }
  void dump(std::ostream& out, int indent = 0) const override;
};

// Frontend `//LCMD_cmd(args)` — expanded to injected stmts before codegen.
struct DirectiveStmt : Stmt {
  std::string command;       // e.g. make_answer
  std::string args;          // raw text inside (...), may be empty
  std::string context_path;  // hierarchical path e.g. User.generate
  StmtKind kind() const override { return StmtKind::Directive; }
  void dump(std::ostream& out, int indent = 0) const override;
};

struct Module {
  std::vector<StmtPtr> stmts;
  // Module lifecycle hooks (`fun __init` / `fun __deinit`) — not ordinary callables.
  std::vector<std::unique_ptr<FuncDeclStmt>> init_hooks;
  std::vector<std::unique_ptr<FuncDeclStmt>> deinit_hooks;
  // Import stems (`import math_lib` → "math_lib") for package-qualified calls.
  std::vector<std::string> imported_modules;
  void dump(std::ostream& out, int indent = 0) const;
};

}  // namespace l::ast
