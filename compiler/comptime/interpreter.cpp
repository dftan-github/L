#include "comptime/interpreter.hpp"

#include <iostream>
#include <string>

#include "comptime/uid.hpp"

// std::cout used by comptime print()

namespace l::comptime {

Interpreter::Interpreter(diag::DiagnosticEngine* diags) : diags_(diags) {}

void Interpreter::setSourceContext(const ast::SourceLocation& loc) {
  ctx_line_ = loc.line;
  ctx_column_ = loc.column;
  ctx_filename_ = loc.filename;
}

void Interpreter::setFuncContext(std::string name) {
  ctx_func_ = std::move(name);
}

void Interpreter::error(const ast::SourceLocation& loc, const std::string& message) {
  had_error_ = true;
  if (diags_) {
    diags_->error(loc, message);
  } else {
    std::cerr << "[ERROR] " << (loc.filename.empty() ? "<comptime>" : loc.filename) << ':'
              << loc.line << ':' << loc.column << " - " << message << '\n';
  }
}

Value Interpreter::evalCall(const ast::CallExpr& call) {
  auto evalArg = [this](const ast::CallArg& a) -> Value {
    if (!a.value) {
      return Value::makeNil();
    }
    return evalExpr(*a.value);
  };

  // [uid-counter]: compile-time builtin — never a runtime LLVM call.
  if (call.callee == "uid_counter") {
    if (!call.args.empty()) {
      error(call.loc, "uid_counter() takes no arguments");
      return Value::makeNil();
    }
    return Value::makeInt(takeUidCounter());
  }
  // [source-line] [source-column] expose directive/comptime source position.
  if (call.callee == "source_line") {
    if (!call.args.empty()) {
      error(call.loc, "source_line() takes no arguments");
      return Value::makeNil();
    }
    return Value::makeInt(static_cast<std::int64_t>(ctx_line_));
  }
  if (call.callee == "source_column") {
    if (!call.args.empty()) {
      error(call.loc, "source_column() takes no arguments");
      return Value::makeNil();
    }
    return Value::makeInt(static_cast<std::int64_t>(ctx_column_));
  }

  // Compile-time fatal error — stops the frontend (no LLVM emission).
  if (call.callee == "compile_error") {
    if (call.args.size() != 1 || !call.args[0].value) {
      error(call.loc, "compile_error(msg) expects exactly one String argument");
      return Value::makeNil();
    }
    Value msg = evalArg(call.args[0]);
    if (had_error_) {
      return Value::makeNil();
    }
    if (msg.kind != ValueKind::String) {
      error(call.loc, "compile_error(msg) expects a String");
      return Value::makeNil();
    }
    const std::string file =
        !call.loc.filename.empty()
            ? call.loc.filename
            : (!ctx_filename_.empty() ? ctx_filename_ : std::string("<unknown>"));
    // Loc already carries __FILE__/__LINE__ into [ERROR] file:line:col -.
    error(call.loc, "Compile-time error: " + msg.s);
    (void)file;
    return Value::makeNil();
  }

  if (call.callee == "comptime_assert") {
    if (call.args.size() != 2 || !call.args[0].value || !call.args[1].value) {
      error(call.loc, "comptime_assert(cond, msg) expects (bool, String)");
      return Value::makeNil();
    }
    Value cond = evalArg(call.args[0]);
    Value msg = evalArg(call.args[1]);
    if (had_error_) {
      return Value::makeNil();
    }
    if (msg.kind != ValueKind::String) {
      error(call.loc, "comptime_assert msg must be String");
      return Value::makeNil();
    }
    if (!cond.isTruthy()) {
      error(call.loc, "Compile-time error: " + msg.s);
    }
    return Value::makeNil();
  }

  // Allowed no-op / diagnostic print during compilation (not runtime I/O).
  if (call.callee == "print") {
    for (const ast::CallArg& a : call.args) {
      Value v = evalArg(a);
      if (had_error_) {
        return Value::makeNil();
      }
      if (v.kind == ValueKind::String) {
        std::cout << v.s << '\n';
      } else if (v.kind == ValueKind::Int) {
        std::cout << v.i << '\n';
      } else if (v.kind == ValueKind::Bool) {
        std::cout << (v.b ? "true" : "false") << '\n';
      }
    }
    return Value::makeNil();
  }

  error(call.loc, "unsupported comptime call '" + call.callee + "'");
  return Value::makeNil();
}

Value Interpreter::evalExpr(const ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::IntLiteral: {
      const auto& lit = static_cast<const ast::IntLiteralExpr&>(expr);
      return Value::makeInt(std::stoll(lit.value));
    }
    case ast::ExprKind::BoolLiteral: {
      const auto& lit = static_cast<const ast::BooleanLiteralExpr&>(expr);
      return Value::makeBool(lit.value);
    }
    case ast::ExprKind::StringLiteral: {
      const auto& lit = static_cast<const ast::StringLiteralExpr&>(expr);
      return Value::makeString(lit.value);
    }
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      if (id.name == "__FILE__") {
        const std::string& file =
            !id.loc.filename.empty()
                ? id.loc.filename
                : (!ctx_filename_.empty() ? ctx_filename_ : std::string("<unknown>"));
        return Value::makeString(file);
      }
      if (id.name == "__FUNC__") {
        return Value::makeString(ctx_func_.empty() ? std::string("<unknown>") : ctx_func_);
      }
      if (id.name == "__LINE__") {
        const std::int64_t line =
            id.loc.line != 0 ? static_cast<std::int64_t>(id.loc.line)
                             : static_cast<std::int64_t>(ctx_line_);
        return Value::makeInt(line);
      }
      auto it = env_.find(id.name);
      if (it == env_.end()) {
        error(expr.loc, "comptime undeclared identifier '" + id.name + "'");
        return Value::makeNil();
      }
      return it->second;
    }
    case ast::ExprKind::Call: {
      return evalCall(static_cast<const ast::CallExpr&>(expr));
    }
    case ast::ExprKind::Binary: {
      const auto& bin = static_cast<const ast::BinaryExpr&>(expr);
      // Short-circuit logic (needed by LCMD path handlers).
      if (bin.op == "&&" || bin.op == "||") {
        Value lhs = evalExpr(*bin.left);
        if (had_error_) {
          return Value::makeNil();
        }
        if (bin.op == "&&") {
          if (!lhs.isTruthy()) {
            return Value::makeBool(false);
          }
          Value rhs = evalExpr(*bin.right);
          if (had_error_) {
            return Value::makeNil();
          }
          return Value::makeBool(rhs.isTruthy());
        }
        if (lhs.isTruthy()) {
          return Value::makeBool(true);
        }
        Value rhs = evalExpr(*bin.right);
        if (had_error_) {
          return Value::makeNil();
        }
        return Value::makeBool(rhs.isTruthy());
      }
      Value lhs = evalExpr(*bin.left);
      Value rhs = evalExpr(*bin.right);
      if (had_error_) {
        return Value::makeNil();
      }
      if (bin.op == "==") {
        if (lhs.kind == ValueKind::String && rhs.kind == ValueKind::String) {
          return Value::makeBool(lhs.s == rhs.s);
        }
        if (lhs.kind == ValueKind::Int && rhs.kind == ValueKind::Int) {
          return Value::makeBool(lhs.i == rhs.i);
        }
        if (lhs.kind == ValueKind::Bool && rhs.kind == ValueKind::Bool) {
          return Value::makeBool(lhs.b == rhs.b);
        }
        error(expr.loc, "comptime '==' type mismatch");
        return Value::makeNil();
      }
      if (bin.op == "!=") {
        if (lhs.kind == ValueKind::String && rhs.kind == ValueKind::String) {
          return Value::makeBool(lhs.s != rhs.s);
        }
        if (lhs.kind == ValueKind::Int && rhs.kind == ValueKind::Int) {
          return Value::makeBool(lhs.i != rhs.i);
        }
        if (lhs.kind == ValueKind::Bool && rhs.kind == ValueKind::Bool) {
          return Value::makeBool(lhs.b != rhs.b);
        }
        error(expr.loc, "comptime '!=' type mismatch");
        return Value::makeNil();
      }
      if (bin.op == "+") {
        if (lhs.kind == ValueKind::String && rhs.kind == ValueKind::String) {
          return Value::makeString(lhs.s + rhs.s);
        }
        // Implicit String + Int (and Int + String) for LCMD code generation.
        if (lhs.kind == ValueKind::String && rhs.kind == ValueKind::Int) {
          return Value::makeString(lhs.s + std::to_string(rhs.i));
        }
        if (lhs.kind == ValueKind::Int && rhs.kind == ValueKind::String) {
          return Value::makeString(std::to_string(lhs.i) + rhs.s);
        }
        if (lhs.kind == ValueKind::Int && rhs.kind == ValueKind::Int) {
          return Value::makeInt(lhs.i + rhs.i);
        }
      }
      if (lhs.kind == ValueKind::Int && rhs.kind == ValueKind::Int) {
        if (bin.op == "-") return Value::makeInt(lhs.i - rhs.i);
        if (bin.op == "*") return Value::makeInt(lhs.i * rhs.i);
        if (bin.op == "/") return Value::makeInt(rhs.i == 0 ? 0 : lhs.i / rhs.i);
        if (bin.op == "<") return Value::makeBool(lhs.i < rhs.i);
        if (bin.op == ">") return Value::makeBool(lhs.i > rhs.i);
        if (bin.op == "<=") return Value::makeBool(lhs.i <= rhs.i);
        if (bin.op == ">=") return Value::makeBool(lhs.i >= rhs.i);
      }
      error(expr.loc, "unsupported comptime binary operator '" + bin.op + "'");
      return Value::makeNil();
    }
    default:
      error(expr.loc, "unsupported comptime expression");
      return Value::makeNil();
  }
}

bool Interpreter::execStmt(const ast::Stmt& stmt) {
  if (has_returned_ || had_error_) {
    return !had_error_;
  }
  switch (stmt.kind()) {
    case ast::StmtKind::Return: {
      const auto& ret = static_cast<const ast::ReturnStmt&>(stmt);
      if (ret.value) {
        return_value_ = evalExpr(*ret.value);
      } else {
        return_value_ = Value::makeNil();
      }
      has_returned_ = true;
      return !had_error_;
    }
    case ast::StmtKind::If: {
      const auto& ifs = static_cast<const ast::IfStmt&>(stmt);
      Value cond = evalExpr(*ifs.cond);
      if (had_error_) {
        return false;
      }
      if (cond.isTruthy()) {
        return execBlock(ifs.then_body);
      }
      return execBlock(ifs.else_body);
    }
    case ast::StmtKind::VarDecl: {
      const auto& decl = static_cast<const ast::VarDeclStmt&>(stmt);
      Value init = Value::makeNil();
      if (decl.init) {
        init = evalExpr(*decl.init);
      }
      env_[decl.name] = std::move(init);
      return !had_error_;
    }
    case ast::StmtKind::ExprStmt: {
      const auto& es = static_cast<const ast::ExprStmt&>(stmt);
      if (es.expr) {
        (void)evalExpr(*es.expr);
      }
      return !had_error_;
    }
    default:
      error(stmt.loc, "unsupported comptime statement");
      return false;
  }
}

bool Interpreter::execBlock(const std::vector<ast::StmtPtr>& stmts) {
  for (const ast::StmtPtr& s : stmts) {
    if (!s) {
      continue;
    }
    if (!execStmt(*s)) {
      return false;
    }
    if (has_returned_) {
      return true;
    }
  }
  return !had_error_;
}

const ast::FuncDeclStmt* Interpreter::findFunction(const ast::Module& module,
                                                   const std::string& name) {
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::FuncDecl) {
      continue;
    }
    const auto& f = static_cast<const ast::FuncDeclStmt&>(*stmt);
    if (f.name == name) {
      return &f;
    }
  }
  return nullptr;
}

bool Interpreter::execStmts(const std::vector<ast::StmtPtr>& stmts) {
  has_returned_ = false;
  had_error_ = false;
  return_value_ = Value::makeNil();
  return execBlock(stmts);
}

Value Interpreter::callFunction(const ast::FuncDeclStmt& fn, const std::vector<Value>& args) {
  if (fn.params.size() != args.size()) {
    error(fn.loc, "comptime argument count mismatch calling " + fn.name);
    return Value::makeNil();
  }

  env_.clear();
  has_returned_ = false;
  had_error_ = false;
  return_value_ = Value::makeNil();
  if (ctx_func_.empty()) {
    ctx_func_ = fn.name;
  }
  for (size_t i = 0; i < fn.params.size(); ++i) {
    env_[fn.params[i].name] = args[i];
  }
  if (!execBlock(fn.body)) {
    return Value::makeNil();
  }
  if (!has_returned_) {
    error(fn.loc, "comptime function '" + fn.name + "' did not return");
    return Value::makeNil();
  }
  return return_value_;
}

}  // namespace l::comptime
