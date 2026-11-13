#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/ast.hpp"
#include "diagnostic/diagnostic.hpp"

namespace l::comptime {

// Minimal tree-walking values for compile-time L callbacks (no LLVM).
enum class ValueKind { Nil, Int, Bool, String };

struct Value {
  ValueKind kind = ValueKind::Nil;
  std::int64_t i = 0;
  bool b = false;
  std::string s;

  static Value makeNil() { return {}; }
  static Value makeInt(std::int64_t v) {
    Value out;
    out.kind = ValueKind::Int;
    out.i = v;
    return out;
  }
  static Value makeBool(bool v) {
    Value out;
    out.kind = ValueKind::Bool;
    out.b = v;
    return out;
  }
  static Value makeString(std::string v) {
    Value out;
    out.kind = ValueKind::String;
    out.s = std::move(v);
    return out;
  }

  bool isTruthy() const {
    switch (kind) {
      case ValueKind::Bool:
        return b;
      case ValueKind::Int:
        return i != 0;
      case ValueKind::String:
        return !s.empty();
      case ValueKind::Nil:
        return false;
    }
    return false;
  }
};

class Interpreter {
public:
  explicit Interpreter(diag::DiagnosticEngine* diags = nullptr);

  // Directive / comptime source position (Rules 91–92) for builtins.
  void setSourceContext(const ast::SourceLocation& loc);
  // Logical function name for `__FUNC__` while interpreting a callback.
  void setFuncContext(std::string name);

  // Execute a function AST with the given args; returns its String (or empty).
  Value callFunction(const ast::FuncDeclStmt& fn, const std::vector<Value>& args);

  // Run a statement list (e.g. `comptime { ... }` body). Returns false on error.
  bool execStmts(const std::vector<ast::StmtPtr>& stmts);

  // Lookup helper for expand: find a top-level function by name.
  static const ast::FuncDeclStmt* findFunction(const ast::Module& module,
                                               const std::string& name);

private:
  Value evalExpr(const ast::Expr& expr);
  Value evalCall(const ast::CallExpr& call);
  bool execStmt(const ast::Stmt& stmt);
  bool execBlock(const std::vector<ast::StmtPtr>& stmts);
  void error(const ast::SourceLocation& loc, const std::string& message);

  diag::DiagnosticEngine* diags_ = nullptr;
  std::unordered_map<std::string, Value> env_;
  Value return_value_;
  bool has_returned_ = false;
  bool had_error_ = false;
  std::uint32_t ctx_line_ = 1;
  std::uint32_t ctx_column_ = 1;
  std::string ctx_filename_;
  std::string ctx_func_;
};

}  // namespace l::comptime
