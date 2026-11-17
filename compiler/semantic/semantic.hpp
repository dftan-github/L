#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ast/ast.hpp"
#include "diagnostic/diagnostic.hpp"

namespace l::semantic {

// Sentinel type used after a name/type failure so later checks do not cascade.
inline constexpr const char* kErrorType = "<error>";

class Analyzer {
public:
  // Mutability / sync data-race checks, undeclared names, etc.
  // Diagnostics are appended to `diags`. Returns false if any Error was recorded.
  bool analyze(ast::Module& module, diag::DiagnosticEngine& diags);

private:
  void error(const ast::SourceLocation& loc, const std::string& message);
  // Merge `partial class` parts with the same name into one ClassDeclStmt.
  bool mergePartialClasses(ast::Module& module);
  bool classConflictsWithNonPartial(
      const ast::ClassDeclStmt& part,
      const std::unordered_map<std::string, std::size_t>& sol);
  bool solidClassConflicts(
      const ast::ClassDeclStmt& cls,
      const std::unordered_map<std::string, std::vector<std::size_t>>& partials,
      const std::unordered_map<std::string, std::size_t>& solids);
  // Reject a class that delegates the same type twice ([link:97-Implicit-Delegation]).
  void checkDelegateFields(const ast::ClassDeclStmt& cls);
  // Inject `pub fun clone() -> Self` when the class does not define clone.
  void injectAutoCloneMethods(ast::Module& module);
  // Inject `pub fun to_string() -> String` when the class does not define to_string.
  void injectAutoToStringMethods(ast::Module& module);
  void checkStmt(ast::Stmt& stmt);
  void checkStmtList(std::vector<ast::StmtPtr>& stmts);
  void checkExpr(ast::Expr& expr);
  void pushScope();
  void popScope();
  void declareLocal(const std::string& name, const std::string& type_name,
                    const ast::SourceLocation& loc);
  bool lookupLocal(const std::string& name, std::string& out_type) const;
  // Collect free locals into LambdaExpr::captures (enclosing scopes).
  void fillLambdaCaptures(ast::LambdaExpr& lambda);
  void collectIdents(const ast::Expr& expr, std::vector<std::string>& ordered,
                     std::unordered_set<std::string>& seen) const;
  void collectIdents(const ast::Stmt& stmt, std::vector<std::string>& ordered,
                     std::unordered_set<std::string>& seen) const;

  diag::DiagnosticEngine* diags_ = nullptr;
  bool in_sync_ = false;
  // Data-race checks apply only inside `spawn { ... }` ([link:42-Closure-Integration-Data-Race-Refinement]).
  bool in_spawn_ = false;
  // `await` is only legal inside an `async fun` body.
  bool in_async_fn_ = false;
  // Inside a loop body: `await` there cannot be resumed in the async MVP.
  bool in_loop_ = false;
  std::unordered_set<std::string> mutable_vars_;
  // Scoped symbol tables (outer → inner). Value may be kErrorType.
  std::vector<std::unordered_map<std::string, std::string>> scopes_;
  // Field names of the class currently being checked (bare `field =` in methods).
  std::unordered_set<std::string> current_fields_;
  // [dsl] `server { ... }` inside a typed config block: the class the
  // nested block's `this` refers to (empty outside config sections).
  std::string config_section_class_;
  // Module-level classes (after partial merge) for property fallback checks.
  std::unordered_map<std::string, const ast::ClassDeclStmt*> classes_;
  // True while checking a property write (`u.x = ...`).
  bool property_write_ = false;
  // `const` / `constd` names (module or local) — reject stores.
  std::unordered_set<std::string> readonly_names_;

  bool classHasMethod(const ast::ClassDeclStmt& cls, const std::string& name) const;
  bool classHasPubField(const ast::ClassDeclStmt& cls, const std::string& name) const;
  bool classHasField(const ast::ClassDeclStmt& cls, const std::string& name) const;
  void checkPropertyAccess(const ast::MemberAccessExpr& access, bool is_write);

  bool classImplementsInterface(const std::string& class_name,
                                const std::string& iface_name) const;
  bool interfaceHasMethod(const std::string& iface_name, const std::string& method) const;
  // Best-effort type of an expression for generic constraint checks.
  std::string inferExprType(const ast::Expr& expr) const;
  // Static type of `objType.member` (getter first, then logical field).
  std::string memberAccessType(const std::string& obj_ty, const std::string& member) const;
  // Static result type of `recv.method(...)`, including `?.` Optional wrapping.
  std::string methodCallResultType(const ast::MethodCallExpr& call) const;
  // Static return type of `recv_ty.method` (instance / static / single delegate).
  std::string methodReturnTypeOn(const std::string& recv_ty,
                                 const std::string& method) const;
  void checkGenericCallConstraints(const ast::CallExpr& call);
  void checkConstrainedMethodCall(const ast::MethodCallExpr& call);
  // Type-checks one function body (own scope, generics, async flag).
  void checkFuncBody(ast::FuncDeclStmt& fn);
  // One `match` arm with its own scope and pattern bindings.
  void checkMatchBranch(ast::MatchBranch& br);
  // Bare identifier reference in expression position.
  void checkIdentifierRef(const ast::IdentifierExpr& id);
  // Receiver of `x.m()` when `x` is a bare identifier.
  void checkMethodCallReceiver(const ast::IdentifierExpr& id);
  // `mod::fn(...)` visibility against the target module's exports.
  void checkScopedCallTarget(const ast::ScopedCallExpr& sc);
  // `a::b::X.m()` — namespace chains are allowed as method-call receivers.
  void checkQualifiedReceiver(const ast::IdentifierExpr& id);
  // `return;` legality plus returned-lambda escape marking.
  void checkReturnStatement(ast::ReturnStmt& ret);
  // Cheap static type of one arithmetic operand (empty when unknown).
  std::string arithmeticOperandType(const ast::Expr& e) const;
  // Numeric operand compatibility for `+ - * /`.
  void checkArithmeticOperands(const ast::Expr& left, const ast::Expr& right,
                               const std::string& op, const ast::SourceLocation& loc);

  // --- Flattening helpers (keeps checkStmt / checkExpr free of deep nesting) ---
  // Undeclared-name check for a bare identifier used as the target of a store
  // (assign / ++ / --). Returns false when the name did not resolve.
  bool checkMutableTarget(const ast::IdentifierExpr& id, const ast::SourceLocation& loc,
                          const char* message_context);
  // Data-race rule: mutable `!` variable touched inside `spawn` outside `sync`.
  void checkRaceViolation(const std::string& name, const ast::SourceLocation& loc) const;
  // `const` / `constd` store rejection.
  void checkReadonlyStore(const std::string& name, const ast::SourceLocation& loc) const;
  // Class lookup for a destructuring RHS (`(a, b) := expr`).
  const ast::ClassDeclStmt* destructureSourceClass(const ast::Expr& init);
  void bindDestructureNames(const ast::DestructureDeclStmt& decl,
                            const ast::ClassDeclStmt* cls);
  // [dsl] Retarget `this` to a nested section type (`server { ... }`).
  void enterConfigSection(const ast::BlockStmt& blk);
  void pushConfigSectionFields(const ast::ClassDeclStmt& cls);

  // Generic function templates (name → AST).
  std::unordered_map<std::string, const ast::FuncDeclStmt*> generic_funcs_;
  // Free functions (incl. extern) — bare name usable as callback / fun(...) value.
  std::unordered_map<std::string, const ast::FuncDeclStmt*> free_funcs_;
  // [dsl] `fun name(fun() body, ...)` — a bare `{ ... }` after it is a
  // config block whose body runs with `this` bound to a config root.
  std::unordered_set<std::string> config_block_funcs_;
  // Cross-module symbol tables: module stem → (exported name → decl).
  // Only `pub` declarations are registered here; that is what makes
  // `mod::fn()` resolvable and what makes `mod::private_fn()` an error.
  std::unordered_map<std::string, std::unordered_map<std::string, const ast::FuncDeclStmt*>>
      module_exports_;
  // Interface declarations (name → AST).
  std::unordered_map<std::string, const ast::InterfaceDeclStmt*> interfaces_;
  // Active generic type params inside a template body: T → IPrintable (may be empty).
  std::unordered_map<std::string, std::string> active_generic_constraints_;
  // Imported package/module stems (`import math_lib` → math_lib.add(...)).
  std::unordered_set<std::string> imported_modules_;
  // `use` aliases: alias → namespace scope (`use net::http::Server` → net::http::Server).
  // Lets an alias head a `::` chain (`Server::start()`) or name a class (`Server(...)`).
  std::unordered_map<std::string, std::string> use_aliases_;
  // Enclosing function while checking its body (escape analysis for returned lambdas).
  ast::FuncDeclStmt* current_fn_ = nullptr;
};

}  // namespace l::semantic
