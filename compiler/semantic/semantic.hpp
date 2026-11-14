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
  // Reject a class that delegates the same type twice ([delegate]).
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
  // Data-race checks apply only inside `spawn { ... }` ([closure-integration]).
  bool in_spawn_ = false;
  std::unordered_set<std::string> mutable_vars_;
  // Scoped symbol tables (outer → inner). Value may be kErrorType.
  std::vector<std::unordered_map<std::string, std::string>> scopes_;
  // Field names of the class currently being checked (bare `field =` in methods).
  std::unordered_set<std::string> current_fields_;
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
  // Static return type of `recv_ty.method` (instance / static / single delegate).
  std::string methodReturnTypeOn(const std::string& recv_ty,
                                 const std::string& method) const;
  void checkGenericCallConstraints(const ast::CallExpr& call);
  void checkConstrainedMethodCall(const ast::MethodCallExpr& call);

  // Generic function templates (name → AST).
  std::unordered_map<std::string, const ast::FuncDeclStmt*> generic_funcs_;
  // Free functions (incl. extern) — bare name usable as callback / fun(...) value.
  std::unordered_map<std::string, const ast::FuncDeclStmt*> free_funcs_;
  // Interface declarations (name → AST).
  std::unordered_map<std::string, const ast::InterfaceDeclStmt*> interfaces_;
  // Active generic type params inside a template body: T → IPrintable (may be empty).
  std::unordered_map<std::string, std::string> active_generic_constraints_;
  // Imported package/module stems (`import math_lib` → math_lib.add(...)).
  std::unordered_set<std::string> imported_modules_;
  // Enclosing function while checking its body (escape analysis for returned lambdas).
  ast::FuncDeclStmt* current_fn_ = nullptr;
};

}  // namespace l::semantic
