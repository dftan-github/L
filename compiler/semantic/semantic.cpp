#include "semantic/semantic.hpp"

#include <iostream>

namespace l::semantic {

void Analyzer::error(const std::string& message) {
  had_error_ = true;
  diagnostics_.push_back(message);
  std::cerr << message << '\n';
}

void Analyzer::checkExpr(const ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::Binary: {
      const auto& bin = static_cast<const ast::BinaryExpr&>(expr);
      if (bin.left) checkExpr(*bin.left);
      if (bin.right) checkExpr(*bin.right);
      break;
    }
    case ast::ExprKind::Call: {
      const auto& call = static_cast<const ast::CallExpr&>(expr);
      for (const auto& arg : call.args) {
        if (arg) checkExpr(*arg);
      }
      break;
    }
    case ast::ExprKind::MemberAccess: {
      const auto& access = static_cast<const ast::MemberAccessExpr&>(expr);
      if (access.object) checkExpr(*access.object);
      break;
    }
    case ast::ExprKind::MethodCall: {
      const auto& call = static_cast<const ast::MethodCallExpr&>(expr);
      if (call.object) checkExpr(*call.object);
      for (const auto& arg : call.args) {
        if (arg) checkExpr(*arg);
      }
      break;
    }
    case ast::ExprKind::Heap: {
      const auto& heap = static_cast<const ast::HeapExpr&>(expr);
      if (heap.inner) checkExpr(*heap.inner);
      break;
    }
    case ast::ExprKind::New: {
      const auto& neu = static_cast<const ast::NewExpr&>(expr);
      for (const auto& arg : neu.args) {
        if (arg) checkExpr(*arg);
      }
      break;
    }
    default:
      break;
  }
}

void Analyzer::checkStmtList(const std::vector<ast::StmtPtr>& stmts) {
  for (const ast::StmtPtr& stmt : stmts) {
    if (stmt) {
      checkStmt(*stmt);
    }
  }
}

void Analyzer::checkStmt(const ast::Stmt& stmt) {
  switch (stmt.kind()) {
    case ast::StmtKind::VarDecl: {
      const auto& decl = static_cast<const ast::VarDeclStmt&>(stmt);
      if (decl.is_mutable) {
        mutable_vars_.insert(decl.name);
      }
      if (decl.init) {
        checkExpr(*decl.init);
      }
      break;
    }
    case ast::StmtKind::Assign: {
      const auto& assign = static_cast<const ast::AssignStmt&>(stmt);
      if (assign.target && assign.target->kind() == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*assign.target);
        if (mutable_vars_.count(id.name) != 0 && !in_sync_) {
          error("Error: Data race detected. Mutable variable '" + id.name +
                "!' accessed outside a 'sync' block.");
        }
      }
      if (assign.target) checkExpr(*assign.target);
      if (assign.value) checkExpr(*assign.value);
      break;
    }
    case ast::StmtKind::Sync: {
      const auto& sync = static_cast<const ast::SyncBlockStmt&>(stmt);
      const bool prev = in_sync_;
      in_sync_ = true;
      checkStmtList(sync.body);
      in_sync_ = prev;
      break;
    }
    case ast::StmtKind::FuncDecl: {
      const auto& fn = static_cast<const ast::FuncDeclStmt&>(stmt);
      // Mutable locals are function-scoped; track within each function body.
      auto saved = mutable_vars_;
      checkStmtList(fn.body);
      mutable_vars_ = std::move(saved);
      break;
    }
    case ast::StmtKind::If: {
      const auto& iff = static_cast<const ast::IfStmt&>(stmt);
      if (iff.cond) checkExpr(*iff.cond);
      checkStmtList(iff.then_body);
      checkStmtList(iff.else_body);
      break;
    }
    case ast::StmtKind::While: {
      const auto& wh = static_cast<const ast::WhileStmt&>(stmt);
      if (wh.cond) checkExpr(*wh.cond);
      checkStmtList(wh.body);
      break;
    }
    case ast::StmtKind::Defer: {
      const auto& def = static_cast<const ast::DeferStmt&>(stmt);
      checkStmtList(def.body);
      break;
    }
    case ast::StmtKind::Return: {
      const auto& ret = static_cast<const ast::ReturnStmt&>(stmt);
      if (ret.value) checkExpr(*ret.value);
      break;
    }
    case ast::StmtKind::ExprStmt: {
      const auto& es = static_cast<const ast::ExprStmt&>(stmt);
      if (es.expr) checkExpr(*es.expr);
      break;
    }
    case ast::StmtKind::ClassDecl: {
      const auto& cls = static_cast<const ast::ClassDeclStmt&>(stmt);
      for (const auto& method : cls.methods) {
        if (method) checkStmt(*method);
      }
      break;
    }
    default:
      break;
  }
}

bool Analyzer::analyze(ast::Module& module) {
  had_error_ = false;
  diagnostics_.clear();
  mutable_vars_.clear();
  in_sync_ = false;

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (stmt) {
      checkStmt(*stmt);
    }
  }
  return !had_error_;
}

}  // namespace l::semantic
