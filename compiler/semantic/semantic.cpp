#include "semantic/semantic.hpp"

namespace l::semantic {

void Analyzer::error(const ast::SourceLocation& loc, const std::string& message) {
  if (diags_) {
    diags_->error(loc, message);
  }
}

void Analyzer::pushScope() {
  scopes_.emplace_back();
}

void Analyzer::popScope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

void Analyzer::declareLocal(const std::string& name, const std::string& type_name,
                            const ast::SourceLocation& loc) {
  if (scopes_.empty()) {
    pushScope();
  }
  auto& scope = scopes_.back();
  if (scope.count(name) != 0) {
    error(loc, "redefinition of '" + name + "'");
    return;
  }
  scope[name] = type_name.empty() ? std::string("i32") : type_name;
}

bool Analyzer::lookupLocal(const std::string& name, std::string& out_type) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      out_type = found->second;
      return true;
    }
  }
  return false;
}

void Analyzer::collectIdents(const ast::Expr& expr, std::vector<std::string>& ordered,
                             std::unordered_set<std::string>& seen) const {
  switch (expr.kind()) {
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      if (seen.insert(id.name).second) {
        ordered.push_back(id.name);
      }
      break;
    }
    case ast::ExprKind::Binary: {
      const auto& bin = static_cast<const ast::BinaryExpr&>(expr);
      if (bin.left) collectIdents(*bin.left, ordered, seen);
      if (bin.right) collectIdents(*bin.right, ordered, seen);
      break;
    }
    case ast::ExprKind::Call: {
      const auto& call = static_cast<const ast::CallExpr&>(expr);
      for (const auto& arg : call.args) {
        if (arg.value) collectIdents(*arg.value, ordered, seen);
      }
      break;
    }
    case ast::ExprKind::MemberAccess: {
      const auto& access = static_cast<const ast::MemberAccessExpr&>(expr);
      if (access.object) collectIdents(*access.object, ordered, seen);
      break;
    }
    case ast::ExprKind::MethodCall: {
      const auto& call = static_cast<const ast::MethodCallExpr&>(expr);
      if (call.object) collectIdents(*call.object, ordered, seen);
      for (const auto& arg : call.args) {
        if (arg.value) collectIdents(*arg.value, ordered, seen);
      }
      break;
    }
    case ast::ExprKind::Heap: {
      const auto& heap = static_cast<const ast::HeapExpr&>(expr);
      if (heap.inner) collectIdents(*heap.inner, ordered, seen);
      break;
    }
    case ast::ExprKind::New: {
      const auto& neu = static_cast<const ast::NewExpr&>(expr);
      for (const auto& arg : neu.args) {
        if (arg.value) collectIdents(*arg.value, ordered, seen);
      }
      break;
    }
    case ast::ExprKind::Index: {
      const auto& idx = static_cast<const ast::IndexExpr&>(expr);
      if (idx.object) collectIdents(*idx.object, ordered, seen);
      if (idx.index) collectIdents(*idx.index, ordered, seen);
      break;
    }
    case ast::ExprKind::Cast: {
      const auto& cast = static_cast<const ast::CastExpr&>(expr);
      if (cast.value) collectIdents(*cast.value, ordered, seen);
      break;
    }
    case ast::ExprKind::Lambda: {
      const auto& lam = static_cast<const ast::LambdaExpr&>(expr);
      for (const ast::StmtPtr& s : lam.body) {
        if (s) collectIdents(*s, ordered, seen);
      }
      break;
    }
    case ast::ExprKind::ArrayLiteral: {
      const auto& lit = static_cast<const ast::ArrayLiteralExpr&>(expr);
      for (const ast::ExprPtr& e : lit.elements) {
        if (e) collectIdents(*e, ordered, seen);
      }
      break;
    }
    case ast::ExprKind::IncDec: {
      const auto& inc = static_cast<const ast::IncDecExpr&>(expr);
      if (inc.target) collectIdents(*inc.target, ordered, seen);
      break;
    }
    case ast::ExprKind::Try: {
      const auto& try_expr = static_cast<const ast::TryExpr&>(expr);
      if (try_expr.inner) collectIdents(*try_expr.inner, ordered, seen);
      break;
    }
    case ast::ExprKind::AddrOf: {
      const auto& addr = static_cast<const ast::AddrOfExpr&>(expr);
      if (addr.operand) collectIdents(*addr.operand, ordered, seen);
      break;
    }
    default:
      break;
  }
}

void Analyzer::collectIdents(const ast::Stmt& stmt, std::vector<std::string>& ordered,
                             std::unordered_set<std::string>& seen) const {
  switch (stmt.kind()) {
    case ast::StmtKind::VarDecl: {
      const auto& decl = static_cast<const ast::VarDeclStmt&>(stmt);
      if (decl.init) collectIdents(*decl.init, ordered, seen);
      break;
    }
    case ast::StmtKind::DestructureDecl: {
      const auto& decl = static_cast<const ast::DestructureDeclStmt&>(stmt);
      if (decl.init) collectIdents(*decl.init, ordered, seen);
      break;
    }
    case ast::StmtKind::Assign: {
      const auto& a = static_cast<const ast::AssignStmt&>(stmt);
      if (a.target) collectIdents(*a.target, ordered, seen);
      if (a.value) collectIdents(*a.value, ordered, seen);
      break;
    }
    case ast::StmtKind::Return: {
      const auto& r = static_cast<const ast::ReturnStmt&>(stmt);
      if (r.value) collectIdents(*r.value, ordered, seen);
      break;
    }
    case ast::StmtKind::ExprStmt: {
      const auto& e = static_cast<const ast::ExprStmt&>(stmt);
      if (e.expr) collectIdents(*e.expr, ordered, seen);
      break;
    }
    case ast::StmtKind::Block: {
      const auto& b = static_cast<const ast::BlockStmt&>(stmt);
      for (const ast::StmtPtr& s : b.stmts) {
        if (s) collectIdents(*s, ordered, seen);
      }
      break;
    }
    case ast::StmtKind::If: {
      const auto& i = static_cast<const ast::IfStmt&>(stmt);
      if (i.cond) collectIdents(*i.cond, ordered, seen);
      for (const ast::StmtPtr& s : i.then_body) {
        if (s) collectIdents(*s, ordered, seen);
      }
      for (const ast::StmtPtr& s : i.else_body) {
        if (s) collectIdents(*s, ordered, seen);
      }
      break;
    }
    case ast::StmtKind::While: {
      const auto& w = static_cast<const ast::WhileStmt&>(stmt);
      if (w.cond) collectIdents(*w.cond, ordered, seen);
      for (const ast::StmtPtr& s : w.body) {
        if (s) collectIdents(*s, ordered, seen);
      }
      break;
    }
    case ast::StmtKind::ForIn: {
      const auto& f = static_cast<const ast::ForInStmt&>(stmt);
      if (f.iterable) collectIdents(*f.iterable, ordered, seen);
      for (const ast::StmtPtr& s : f.body) {
        if (s) collectIdents(*s, ordered, seen);
      }
      break;
    }
    case ast::StmtKind::Match: {
      const auto& m = static_cast<const ast::MatchStmt&>(stmt);
      if (m.scrutinee) collectIdents(*m.scrutinee, ordered, seen);
      for (const ast::MatchBranch& br : m.branches) {
        if (!br.is_wildcard && br.pattern) collectIdents(*br.pattern, ordered, seen);
        for (const ast::StmtPtr& s : br.body) {
          if (s) collectIdents(*s, ordered, seen);
        }
      }
      break;
    }
    case ast::StmtKind::Defer: {
      const auto& d = static_cast<const ast::DeferStmt&>(stmt);
      for (const ast::StmtPtr& s : d.body) {
        if (s) collectIdents(*s, ordered, seen);
      }
      break;
    }
    case ast::StmtKind::Sync: {
      const auto& s = static_cast<const ast::SyncBlockStmt&>(stmt);
      for (const ast::StmtPtr& b : s.body) {
        if (b) collectIdents(*b, ordered, seen);
      }
      break;
    }
    case ast::StmtKind::Spawn: {
      const auto& s = static_cast<const ast::SpawnBlockStmt&>(stmt);
      for (const ast::StmtPtr& b : s.body) {
        if (b) collectIdents(*b, ordered, seen);
      }
      break;
    }
    default:
      break;
  }
}

void Analyzer::fillLambdaCaptures(ast::LambdaExpr& lambda) {
  std::unordered_set<std::string> params(lambda.params.begin(), lambda.params.end());
  std::vector<std::string> used;
  std::unordered_set<std::string> seen;
  for (const ast::StmtPtr& s : lambda.body) {
    if (s) collectIdents(*s, used, seen);
  }
  lambda.captures.clear();
  for (const std::string& name : used) {
    if (params.count(name) != 0) {
      continue;
    }
    std::string ty;
    if (lookupLocal(name, ty) && ty != kErrorType) {
      lambda.captures.push_back(name);
    }
  }
}

void Analyzer::checkExpr(ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      // Built-in context macros: always String, never look up as locals.
      if (id.name == "__FILE__" || id.name == "__FUNC__" || id.name == "__LINE__") {
        break;
      }
      if (current_fields_.count(id.name) != 0) {
        break;  // bare field reference inside a method
      }
      std::string ty;
      if (!lookupLocal(id.name, ty)) {
        // Bare free-function name (e.g. pass `my_callback` to extern C).
        if (free_funcs_.count(id.name) != 0 || classes_.count(id.name) != 0) {
          break;
        }
        error(expr.loc, "undeclared identifier '" + id.name + "'");
      } else if (ty == kErrorType) {
        // Already poisoned — skip cascading diagnostics.
      }
      break;
    }
    case ast::ExprKind::Binary: {
      auto& bin = static_cast<ast::BinaryExpr&>(expr);
      if (bin.left) checkExpr(*bin.left);
      if (bin.right) checkExpr(*bin.right);
      // Numeric ops: require both sides numeric (or leave to string-concat path).
      if (bin.op == "+" || bin.op == "-" || bin.op == "*" || bin.op == "/") {
        auto side_ty = [this](const ast::Expr& e) -> std::string {
          if (e.kind() == ast::ExprKind::IntLiteral) {
            return "i32";
          }
          if (e.kind() == ast::ExprKind::FloatLiteral) {
            return "f64";
          }
          if (e.kind() == ast::ExprKind::StringLiteral) {
            return "String";
          }
          if (e.kind() == ast::ExprKind::Identifier) {
            const auto& id = static_cast<const ast::IdentifierExpr&>(e);
            if (id.name == "__FILE__" || id.name == "__FUNC__") {
              return "String";
            }
            if (id.name == "__LINE__") {
              return "i32";
            }
            std::string ty;
            if (lookupLocal(id.name, ty)) {
              return ty;
            }
          }
          if (e.kind() == ast::ExprKind::Binary) {
            return "num";  // nested; refined in codegen
          }
          return {};
        };
        const std::string lt = side_ty(*bin.left);
        const std::string rt = side_ty(*bin.right);
        auto is_num = [](const std::string& t) {
          return t == "i32" || t == "i64" || t == "f64" || t == "num";
        };
        auto is_str = [](const std::string& t) { return t == "String" || t == "ptr"; };
        if (bin.op == "+" && (is_str(lt) || is_str(rt))) {
          break;  // string concat / interpolation
        }
        if (!lt.empty() && !rt.empty() && !is_num(lt) && !is_str(lt)) {
          error(expr.loc, "incompatible operand type '" + lt + "' for '" + bin.op + "'");
        } else if (!lt.empty() && !rt.empty() && !is_num(rt) && !is_str(rt)) {
          error(expr.loc, "incompatible operand type '" + rt + "' for '" + bin.op + "'");
        } else if (!lt.empty() && !rt.empty() && is_num(lt) != is_num(rt) &&
                   !(is_str(lt) || is_str(rt))) {
          error(expr.loc, "incompatible types for '" + bin.op + "': '" + lt + "' and '" + rt +
                              "'");
        }
      }
      break;
    }
    case ast::ExprKind::Call: {
      auto& call = static_cast<ast::CallExpr&>(expr);
      // Rule 94: uid_counter() is compile-time only (LCMD / comptime interpreter).
      if (call.callee == "uid_counter") {
        error(call.loc, "uid_counter() is only allowed in compile-time context");
        break;
      }
      if (call.callee == "compile_error" || call.callee == "comptime_assert") {
        error(call.loc, call.callee + "() is only allowed in compile-time context");
        break;
      }
      for (auto& arg : call.args) {
        if (arg.value) checkExpr(*arg.value);
      }
      checkGenericCallConstraints(call);
      break;
    }
    case ast::ExprKind::MemberAccess: {
      auto& access = static_cast<ast::MemberAccessExpr&>(expr);
      if (access.object) checkExpr(*access.object);
      checkPropertyAccess(access, property_write_);
      break;
    }
    case ast::ExprKind::MethodCall: {
      auto& call = static_cast<ast::MethodCallExpr&>(expr);
      // Receiver may be a local *or* a class name for associated calls (`Variant.from_i32`).
      if (call.object) {
        if (call.object->kind() == ast::ExprKind::Identifier) {
          const auto& id = static_cast<const ast::IdentifierExpr&>(*call.object);
          std::string ty;
          if (!lookupLocal(id.name, ty) && classes_.count(id.name) == 0 &&
              current_fields_.count(id.name) == 0 &&
              imported_modules_.count(id.name) == 0) {
            error(call.object->loc, "undeclared identifier '" + id.name + "'");
          }
        } else {
          checkExpr(*call.object);
        }
      }
      for (auto& arg : call.args) {
        if (arg.value) checkExpr(*arg.value);
      }
      checkConstrainedMethodCall(call);
      break;
    }
    case ast::ExprKind::Heap: {
      auto& heap = static_cast<ast::HeapExpr&>(expr);
      if (heap.inner) checkExpr(*heap.inner);
      break;
    }
    case ast::ExprKind::New: {
      auto& neu = static_cast<ast::NewExpr&>(expr);
      for (auto& arg : neu.args) {
        if (arg.value) checkExpr(*arg.value);
      }
      break;
    }
    case ast::ExprKind::Index: {
      auto& idx = static_cast<ast::IndexExpr&>(expr);
      if (idx.object) checkExpr(*idx.object);
      if (idx.index) checkExpr(*idx.index);
      break;
    }
    case ast::ExprKind::Cast: {
      auto& cast = static_cast<ast::CastExpr&>(expr);
      if (cast.value) {
        checkExpr(*cast.value);
      }
      break;
    }
    case ast::ExprKind::Lambda: {
      auto& lam = static_cast<ast::LambdaExpr&>(expr);
      fillLambdaCaptures(lam);
      pushScope();
      for (const std::string& p : lam.params) {
        declareLocal(p, "i32", lam.loc);
      }
      checkStmtList(lam.body);
      popScope();
      break;
    }
    case ast::ExprKind::ArrayLiteral: {
      auto& lit = static_cast<ast::ArrayLiteralExpr&>(expr);
      for (auto& e : lit.elements) {
        if (e) checkExpr(*e);
      }
      break;
    }
    case ast::ExprKind::IncDec: {
      auto& inc = static_cast<ast::IncDecExpr&>(expr);
      if (!inc.target) {
        break;
      }
      // Same mutability / race rules as assignment to the target.
      if (inc.target->kind() == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*inc.target);
        if (current_fields_.count(id.name) == 0) {
          std::string ty;
          if (!lookupLocal(id.name, ty)) {
            error(inc.loc, "undeclared identifier '" + id.name + "'");
          } else if (readonly_names_.count(id.name) != 0) {
            error(inc.loc, "cannot modify constant '" + id.name + "'");
          } else if (ty != kErrorType && in_spawn_ && mutable_vars_.count(id.name) != 0 &&
                     !in_sync_) {
            error(inc.loc, "Data race detected. Mutable variable '" + id.name +
                               "!' accessed outside a 'sync' block inside 'spawn'.");
          }
        }
      } else {
        const bool prev_write = property_write_;
        property_write_ = inc.target->kind() == ast::ExprKind::MemberAccess;
        checkExpr(*inc.target);
        property_write_ = prev_write;
      }
      break;
    }
    case ast::ExprKind::Try: {
      auto& try_expr = static_cast<ast::TryExpr&>(expr);
      if (try_expr.inner) {
        checkExpr(*try_expr.inner);
      }
      break;
    }
    case ast::ExprKind::Spawn: {
      auto& sp = static_cast<ast::SpawnExpr&>(expr);
      const bool prev = in_spawn_;
      in_spawn_ = true;
      checkStmtList(sp.body);
      in_spawn_ = prev;
      break;
    }
    case ast::ExprKind::AddrOf: {
      auto& addr = static_cast<ast::AddrOfExpr&>(expr);
      if (addr.operand) {
        checkExpr(*addr.operand);
      }
      break;
    }
    default:
      break;
  }
}

void Analyzer::checkStmtList(std::vector<ast::StmtPtr>& stmts) {
  for (ast::StmtPtr& stmt : stmts) {
    if (stmt) {
      checkStmt(*stmt);
    }
  }
}

void Analyzer::checkStmt(ast::Stmt& stmt) {
  switch (stmt.kind()) {
    case ast::StmtKind::VarDecl: {
      auto& decl = static_cast<ast::VarDeclStmt&>(stmt);
      if (decl.init) {
        checkExpr(*decl.init);
      }
      std::string ty = decl.type_name;
      if (ty.empty() && decl.init) {
        if (decl.init->kind() == ast::ExprKind::Spawn) {
          ty = "ptr";
        } else {
          ty = inferExprType(*decl.init);
        }
      }
      if (ty.empty()) {
        ty = "i32";
      }
      declareLocal(decl.name, ty, decl.loc);
      if (decl.decl_kind == ast::VarDeclKind::Const ||
          decl.decl_kind == ast::VarDeclKind::Constd) {
        readonly_names_.insert(decl.name);
      }
      if (decl.is_mutable) {
        mutable_vars_.insert(decl.name);
      }
      break;
    }
    case ast::StmtKind::DestructureDecl: {
      auto& decl = static_cast<ast::DestructureDeclStmt&>(stmt);
      if (decl.init) {
        checkExpr(*decl.init);
      }
      if (decl.bindings.size() < 2) {
        error(decl.loc, "destructure requires at least two bindings");
      }

      // Resolve RHS class so bindings get real field types (declaration order).
      const ast::ClassDeclStmt* cls = nullptr;
      if (decl.init && decl.init->kind() == ast::ExprKind::New) {
        const auto& neu = static_cast<const ast::NewExpr&>(*decl.init);
        auto cit = classes_.find(neu.class_name);
        if (cit != classes_.end()) {
          cls = cit->second;
        }
      } else if (decl.init && decl.init->kind() == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*decl.init);
        std::string ty;
        if (lookupLocal(id.name, ty)) {
          auto cit = classes_.find(ty);
          if (cit != classes_.end()) {
            cls = cit->second;
          }
        }
      }

      if (cls) {
        if (decl.bindings.size() != cls->fields.size()) {
          error(decl.loc, "destructure binding count (" +
                              std::to_string(decl.bindings.size()) +
                              ") does not match field count of '" + cls->name + "' (" +
                              std::to_string(cls->fields.size()) + ")");
        }
        for (std::size_t i = 0; i < decl.bindings.size(); ++i) {
          const ast::DestructureBinding& b = decl.bindings[i];
          std::string fty = "i32";
          if (i < cls->fields.size() && cls->fields[i]) {
            fty = cls->fields[i]->type_name.empty() ? "i32" : cls->fields[i]->type_name;
          }
          declareLocal(b.name, fty, b.loc);
          if (b.is_mutable) {
            mutable_vars_.insert(b.name);
          }
        }
      } else {
        for (const ast::DestructureBinding& b : decl.bindings) {
          declareLocal(b.name, "i32", b.loc);
          if (b.is_mutable) {
            mutable_vars_.insert(b.name);
          }
        }
      }
      break;
    }
    case ast::StmtKind::Assign: {
      auto& assign = static_cast<ast::AssignStmt&>(stmt);
      if (assign.target && assign.target->kind() == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*assign.target);
        if (current_fields_.count(id.name) == 0) {
          std::string ty;
          if (!lookupLocal(id.name, ty)) {
            error(assign.loc, "undeclared identifier '" + id.name + "'");
            declareLocal(id.name, kErrorType, assign.loc);
          } else if (readonly_names_.count(id.name) != 0) {
            error(assign.loc, "cannot assign to constant '" + id.name + "'");
          } else if (ty != kErrorType && in_spawn_ && mutable_vars_.count(id.name) != 0 &&
                     !in_sync_) {
            error(assign.loc, "Data race detected. Mutable variable '" + id.name +
                                  "!' accessed outside a 'sync' block inside 'spawn'.");
          }
        }
      } else if (assign.target) {
        const bool prev_write = property_write_;
        property_write_ = assign.target->kind() == ast::ExprKind::MemberAccess;
        checkExpr(*assign.target);
        property_write_ = prev_write;
      }
      if (assign.value) checkExpr(*assign.value);
      break;
    }
    case ast::StmtKind::Sync: {
      auto& sync = static_cast<ast::SyncBlockStmt&>(stmt);
      const bool prev = in_sync_;
      in_sync_ = true;
      checkStmtList(sync.body);
      in_sync_ = prev;
      break;
    }
    case ast::StmtKind::Spawn: {
      auto& sp = static_cast<ast::SpawnBlockStmt&>(stmt);
      const bool prev = in_spawn_;
      in_spawn_ = true;
      checkStmtList(sp.body);
      in_spawn_ = prev;
      break;
    }
    case ast::StmtKind::FuncDecl: {
      auto& fn = static_cast<ast::FuncDeclStmt&>(stmt);
      // Frontend-only LCMD / class-hook callbacks: never type-check bodies
      // (may contain compile-time builtins like uid_counter()).
      if (fn.name == "__compile_time_handler" || fn.name == "__on_class_parsed") {
        break;
      }
      auto saved_mut = mutable_vars_;
      auto saved_constraints = active_generic_constraints_;
      ast::FuncDeclStmt* saved_fn = current_fn_;
      current_fn_ = &fn;
      for (const ast::GenericParam& gp : fn.generics) {
        active_generic_constraints_[gp.name] = gp.constraint;
      }
      pushScope();
      for (const ast::Param& p : fn.params) {
        declareLocal(p.name, p.type_name.empty() ? "i32" : p.type_name, p.loc);
        if (p.is_mutable) {
          mutable_vars_.insert(p.name);
        }
      }
      checkStmtList(fn.body);
      popScope();
      mutable_vars_ = std::move(saved_mut);
      active_generic_constraints_ = std::move(saved_constraints);
      current_fn_ = saved_fn;
      break;
    }
    case ast::StmtKind::If: {
      auto& iff = static_cast<ast::IfStmt&>(stmt);
      if (iff.cond) checkExpr(*iff.cond);
      pushScope();
      checkStmtList(iff.then_body);
      popScope();
      pushScope();
      checkStmtList(iff.else_body);
      popScope();
      break;
    }
    case ast::StmtKind::Match: {
      auto& m = static_cast<ast::MatchStmt&>(stmt);
      if (m.scrutinee) {
        checkExpr(*m.scrutinee);
      }
      for (ast::MatchBranch& br : m.branches) {
        if (!br.is_wildcard && br.pattern) {
          checkExpr(*br.pattern);
        }
        pushScope();
        checkStmtList(br.body);
        popScope();
      }
      break;
    }
    case ast::StmtKind::While: {
      auto& wh = static_cast<ast::WhileStmt&>(stmt);
      if (wh.cond) checkExpr(*wh.cond);
      pushScope();
      checkStmtList(wh.body);
      popScope();
      break;
    }
    case ast::StmtKind::ForIn: {
      auto& fin = static_cast<ast::ForInStmt&>(stmt);
      if (fin.iterable) checkExpr(*fin.iterable);
      pushScope();
      declareLocal(fin.item_name, "i32", fin.loc);
      checkStmtList(fin.body);
      popScope();
      break;
    }
    case ast::StmtKind::Defer: {
      auto& def = static_cast<ast::DeferStmt&>(stmt);
      checkStmtList(def.body);
      break;
    }
    case ast::StmtKind::Return: {
      auto& ret = static_cast<ast::ReturnStmt&>(stmt);
      if (ret.value) {
        checkExpr(*ret.value);
        // Rule 76: returning a capturing lambda → mark escape + fat-closure ABI.
        if (ret.value->kind() == ast::ExprKind::Lambda) {
          auto& lam = static_cast<ast::LambdaExpr&>(*ret.value);
          if (!lam.captures.empty()) {
            lam.escapes = true;
            if (current_fn_) {
              current_fn_->returns_fat_closure = true;
            }
          }
        }
      }
      break;
    }
    case ast::StmtKind::ExprStmt: {
      auto& es = static_cast<ast::ExprStmt&>(stmt);
      if (es.expr) checkExpr(*es.expr);
      break;
    }
    case ast::StmtKind::Block: {
      auto& blk = static_cast<ast::BlockStmt&>(stmt);
      checkStmtList(blk.stmts);
      break;
    }
    case ast::StmtKind::ClassDecl: {
      auto& cls = static_cast<ast::ClassDeclStmt&>(stmt);
      auto saved_fields = current_fields_;
      current_fields_.clear();
      for (const auto& field : cls.fields) {
        if (field) {
          current_fields_.insert(field->name);
        }
      }
      for (auto& method : cls.methods) {
        if (method) checkStmt(*method);
      }
      current_fields_ = std::move(saved_fields);
      break;
    }
    default:
      break;
  }
}

bool Analyzer::mergePartialClasses(ast::Module& module) {
  std::unordered_map<std::string, std::vector<std::size_t>> partial_idxs;
  std::unordered_map<std::string, std::size_t> solid_idxs;

  for (std::size_t i = 0; i < module.stmts.size(); ++i) {
    ast::StmtPtr& stmt = module.stmts[i];
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    auto& cls = static_cast<ast::ClassDeclStmt&>(*stmt);
    if (cls.is_partial) {
      if (solid_idxs.count(cls.name) != 0) {
        error(cls.loc, "partial class '" + cls.name +
                           "' conflicts with a non-partial class of the same name");
        continue;
      }
      partial_idxs[cls.name].push_back(i);
    } else {
      if (partial_idxs.count(cls.name) != 0) {
        error(cls.loc, "class '" + cls.name +
                           "' conflicts with a partial class of the same name");
        continue;
      }
      if (solid_idxs.count(cls.name) != 0) {
        error(cls.loc, "redefinition of class '" + cls.name + "'");
        continue;
      }
      solid_idxs[cls.name] = i;
    }
  }

  bool ok = true;
  for (auto& [name, idxs] : partial_idxs) {
    if (idxs.empty()) {
      continue;
    }
    auto& primary = static_cast<ast::ClassDeclStmt&>(*module.stmts[idxs[0]]);
    std::unordered_set<std::string> field_names;
    std::unordered_set<std::string> method_names;
    std::unordered_set<std::string> iface_names;

    for (const auto& field : primary.fields) {
      if (field) {
        field_names.insert(field->name);
      }
    }
    for (const auto& method : primary.methods) {
      if (method) {
        method_names.insert(method->name);
      }
    }
    for (const std::string& iface : primary.interfaces) {
      iface_names.insert(iface);
    }

    for (std::size_t k = 1; k < idxs.size(); ++k) {
      auto& part = static_cast<ast::ClassDeclStmt&>(*module.stmts[idxs[k]]);
      if (part.generics.size() != primary.generics.size()) {
        error(part.loc, "partial class '" + name + "' generic parameter mismatch");
        ok = false;
        continue;
      }
      for (std::size_t g = 0; g < part.generics.size(); ++g) {
        if (part.generics[g].name != primary.generics[g].name ||
            part.generics[g].constraint != primary.generics[g].constraint) {
          error(part.loc, "partial class '" + name + "' generic parameter mismatch");
          ok = false;
        }
      }
      for (const std::string& iface : part.interfaces) {
        if (iface_names.insert(iface).second) {
          primary.interfaces.push_back(iface);
        }
      }
      for (auto& field : part.fields) {
        if (!field) {
          continue;
        }
        if (!field_names.insert(field->name).second) {
          error(field->loc, "duplicate field '" + field->name + "' in partial class '" +
                                name + "'");
          ok = false;
          continue;
        }
        primary.fields.push_back(std::move(field));
      }
      for (auto& method : part.methods) {
        if (!method) {
          continue;
        }
        if (!method_names.insert(method->name).second) {
          error(method->loc, "duplicate method '" + method->name +
                                 "' in partial class '" + name + "'");
          ok = false;
          continue;
        }
        primary.methods.push_back(std::move(method));
      }
      if (part.is_pub) {
        primary.is_pub = true;
      }
      module.stmts[idxs[k]].reset();
    }

    primary.is_partial = false;
  }

  // Compact removed partial parts.
  std::vector<ast::StmtPtr> compacted;
  compacted.reserve(module.stmts.size());
  for (ast::StmtPtr& stmt : module.stmts) {
    if (stmt) {
      compacted.push_back(std::move(stmt));
    }
  }
  module.stmts = std::move(compacted);
  return ok;
}

bool Analyzer::classImplementsInterface(const std::string& class_name,
                                        const std::string& iface_name) const {
  auto it = classes_.find(class_name);
  if (it == classes_.end() || !it->second) {
    return false;
  }
  for (const std::string& i : it->second->interfaces) {
    if (i == iface_name) {
      return true;
    }
  }
  return false;
}

bool Analyzer::interfaceHasMethod(const std::string& iface_name,
                                  const std::string& method) const {
  auto it = interfaces_.find(iface_name);
  if (it == interfaces_.end() || !it->second) {
    return false;
  }
  for (const auto& m : it->second->methods) {
    if (m && m->name == method) {
      return true;
    }
  }
  return false;
}

std::string Analyzer::inferExprType(const ast::Expr& expr) const {
  switch (expr.kind()) {
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      std::string ty;
      if (lookupLocal(id.name, ty)) {
        return ty;
      }
      return {};
    }
    case ast::ExprKind::New:
      return static_cast<const ast::NewExpr&>(expr).class_name;
    case ast::ExprKind::IntLiteral:
      return "i32";
    case ast::ExprKind::FloatLiteral:
      return "f64";
    case ast::ExprKind::BoolLiteral:
      return "bool";
    case ast::ExprKind::StringLiteral:
      return "String";
    case ast::ExprKind::Heap: {
      const auto& heap = static_cast<const ast::HeapExpr&>(expr);
      if (heap.inner) {
        return inferExprType(*heap.inner);
      }
      return {};
    }
    case ast::ExprKind::MemberAccess: {
      const auto& access = static_cast<const ast::MemberAccessExpr&>(expr);
      if (!access.object) {
        return {};
      }
      const std::string obj_ty = inferExprType(*access.object);
      auto cit = classes_.find(obj_ty);
      if (cit == classes_.end() || !cit->second) {
        return {};
      }
      const ast::ClassDeclStmt& cls = *cit->second;
      auto fieldType = [&](const std::string& name) -> std::string {
        for (const auto& f : cls.fields) {
          if (f && f->name == name) {
            return f->type_name.empty() ? std::string("i32") : f->type_name;
          }
        }
        return {};
      };
      // Trailing `_` is forbidden in source; physical fields are never written that way.
      if (!access.member.empty() && access.member.back() == '_') {
        return {};
      }
      // Property: get_x → return type, else logical field `x` (physical x_).
      const std::string getter = "get_" + access.member;
      for (const auto& m : cls.methods) {
        if (m && m->name == getter) {
          return m->return_type.empty() ? std::string("void") : m->return_type;
        }
      }
      return fieldType(access.member);
    }
    default:
      return {};
  }
}

void Analyzer::checkGenericCallConstraints(const ast::CallExpr& call) {
  auto git = generic_funcs_.find(call.callee);
  if (git == generic_funcs_.end() || !git->second) {
    return;
  }
  const ast::FuncDeclStmt& tmpl = *git->second;
  if (tmpl.generics.empty()) {
    return;
  }
  // Positionals only for Phase 1 constraint checks (KW reorder is codegen's job).
  if (call.args.size() != tmpl.params.size()) {
    return;  // arity errors left to codegen
  }
  for (const ast::GenericParam& gp : tmpl.generics) {
    if (gp.constraint.empty()) {
      continue;
    }
    std::size_t param_i = tmpl.params.size();
    for (std::size_t i = 0; i < tmpl.params.size(); ++i) {
      if (tmpl.params[i].type_name == gp.name) {
        param_i = i;
        break;
      }
    }
    if (param_i >= call.args.size() || !call.args[param_i].value) {
      continue;
    }
    const std::string concrete = inferExprType(*call.args[param_i].value);
    if (concrete.empty() || concrete == kErrorType) {
      continue;
    }
    if (classes_.count(concrete) == 0) {
      error(call.loc, "generic '" + gp.name + "' requires a class type, got '" + concrete + "'");
      continue;
    }
    if (!classImplementsInterface(concrete, gp.constraint)) {
      error(call.loc, "Type '" + concrete + "' does not implement interface '" + gp.constraint +
                          "'");
    }
  }
}

void Analyzer::checkConstrainedMethodCall(const ast::MethodCallExpr& call) {
  if (!call.object || call.object->kind() != ast::ExprKind::Identifier) {
    return;
  }
  const auto& id = static_cast<const ast::IdentifierExpr&>(*call.object);
  std::string ty;
  if (!lookupLocal(id.name, ty)) {
    return;
  }
  auto git = active_generic_constraints_.find(ty);
  if (git == active_generic_constraints_.end()) {
    return;  // concrete class / other — not a type parameter in this template
  }
  const std::string& constraint = git->second;
  if (constraint.empty()) {
    error(call.loc, "cannot call method '" + call.method + "' on unconstrained type parameter '" +
                         ty + "'");
    return;
  }
  if (!interfaceHasMethod(constraint, call.method)) {
    error(call.loc, "interface '" + constraint + "' has no method '" + call.method + "'");
  }
}

bool Analyzer::classHasMethod(const ast::ClassDeclStmt& cls, const std::string& name) const {
  for (const auto& m : cls.methods) {
    if (m && m->name == name) {
      return true;
    }
  }
  return false;
}

bool Analyzer::classHasPubField(const ast::ClassDeclStmt& cls, const std::string& name) const {
  for (const auto& f : cls.fields) {
    if (f && f->name == name && f->is_pub) {
      return true;
    }
  }
  return false;
}

bool Analyzer::classHasField(const ast::ClassDeclStmt& cls, const std::string& name) const {
  for (const auto& f : cls.fields) {
    if (f && f->name == name) {
      return true;
    }
  }
  return false;
}

void Analyzer::checkPropertyAccess(const ast::MemberAccessExpr& access, bool is_write) {
  if (!access.object || access.member.empty()) {
    return;
  }
  // Rule 70: trailing `_` identifiers are forbidden (bare `_` is match wildcard only).
  if (access.member.size() > 1 && access.member.back() == '_') {
    error(access.loc, "identifier ending with '_' is forbidden: '" + access.member + "'");
    return;
  }
  std::string ty;
  if (access.object->kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(*access.object);
    if (!lookupLocal(id.name, ty) || ty == kErrorType) {
      return;
    }
  } else {
    // Nested: `app.rs232.baud` — object type from property/field chain.
    ty = inferExprType(*access.object);
    if (ty.empty() || ty == kErrorType) {
      return;
    }
  }
  auto cit = classes_.find(ty);
  if (cit == classes_.end() || !cit->second) {
    return;
  }
  const ast::ClassDeclStmt& cls = *cit->second;
  const std::string method = is_write ? ("set_" + access.member) : ("get_" + access.member);
  if (classHasMethod(cls, method)) {
    return;
  }
  // Logical field `age` (physical age_ hidden in LLVM).
  if (classHasField(cls, access.member)) {
    return;
  }
  error(access.loc, "unknown property '" + access.member + "' on '" + ty + "'");
}

void Analyzer::injectAutoCloneMethods(ast::Module& module) {
  for (ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    auto& cls = static_cast<ast::ClassDeclStmt&>(*stmt);
    if (classHasMethod(cls, "clone")) {
      continue;  // allow user-defined deep-copy
    }

    // Self → concrete class name, or Class<T,...> for generic templates.
    std::string self_ty = cls.name;
    if (!cls.generics.empty()) {
      self_ty.push_back('<');
      for (std::size_t i = 0; i < cls.generics.size(); ++i) {
        if (i != 0) {
          self_ty.push_back(',');
        }
        self_ty += cls.generics[i].name;
      }
      self_ty.push_back('>');
    }

    auto clone = std::make_unique<ast::FuncDeclStmt>();
    clone->loc = cls.loc;
    clone->name = "clone";
    clone->return_type = std::move(self_ty);
    clone->is_method = true;
    clone->is_pub = true;
    clone->is_compiler_generated = true;
    clone->module_name = cls.module_name;
    cls.methods.push_back(std::move(clone));
  }
}

void Analyzer::injectAutoToStringMethods(ast::Module& module) {
  // Return type is String — only inject when the String class is in this module.
  bool has_string = false;
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (stmt && stmt->kind() == ast::StmtKind::ClassDecl &&
        static_cast<const ast::ClassDeclStmt&>(*stmt).name == "String") {
      has_string = true;
      break;
    }
  }
  if (!has_string) {
    return;
  }

  for (ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    auto& cls = static_cast<ast::ClassDeclStmt&>(*stmt);
    if (classHasMethod(cls, "to_string")) {
      continue;  // user-defined wins
    }
    // Skip String itself — it already exposes data_ / printSelf; avoid recursion.
    if (cls.name == "String") {
      continue;
    }
    // Phase 1: only concrete classes. Injecting into generic templates (Array/Map/…)
    // would emit to_string while specializing nested returns (e.g. Array<String>)
    // before String methods are declared.
    if (!cls.generics.empty()) {
      continue;
    }

    auto ts = std::make_unique<ast::FuncDeclStmt>();
    ts->loc = cls.loc;
    ts->name = "to_string";
    ts->return_type = "String";
    ts->is_method = true;
    ts->is_pub = true;
    ts->is_compiler_generated = true;
    ts->module_name = cls.module_name;
    cls.methods.push_back(std::move(ts));
  }
}

bool Analyzer::analyze(ast::Module& module, diag::DiagnosticEngine& diags) {
  diags_ = &diags;
  mutable_vars_.clear();
  scopes_.clear();
  classes_.clear();
  interfaces_.clear();
  generic_funcs_.clear();
  free_funcs_.clear();
  active_generic_constraints_.clear();
  imported_modules_.clear();
  readonly_names_.clear();
  in_sync_ = false;
  in_spawn_ = false;
  property_write_ = false;
  pushScope();  // module / global locals (mostly functions)

  const std::size_t errors_before = diags.errorCount();
  (void)mergePartialClasses(module);
  injectAutoCloneMethods(module);
  injectAutoToStringMethods(module);
  for (const std::string& m : module.imported_modules) {
    imported_modules_.insert(m);
  }
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    if (stmt->kind() == ast::StmtKind::ClassDecl) {
      const auto& cls = static_cast<const ast::ClassDeclStmt&>(*stmt);
      classes_[cls.name] = &cls;
    } else if (stmt->kind() == ast::StmtKind::InterfaceDecl) {
      const auto& iface = static_cast<const ast::InterfaceDeclStmt&>(*stmt);
      interfaces_[iface.name] = &iface;
    } else if (stmt->kind() == ast::StmtKind::FuncDecl) {
      const auto& fn = static_cast<const ast::FuncDeclStmt&>(*stmt);
      if (!fn.is_method) {
        free_funcs_[fn.name] = &fn;
        if (!fn.generics.empty()) {
          generic_funcs_[fn.name] = &fn;
        }
      }
    } else if (stmt->kind() == ast::StmtKind::ExternModule) {
      const auto& ext = static_cast<const ast::ExternModuleStmt&>(*stmt);
      for (const auto& decl : ext.decls) {
        if (decl && !decl->is_method) {
          free_funcs_[decl->name] = decl.get();
        }
      }
    }
  }
  for (ast::StmtPtr& stmt : module.stmts) {
    if (stmt) {
      checkStmt(*stmt);
    }
  }
  for (auto& h : module.init_hooks) {
    if (h) {
      checkStmt(*h);
    }
  }
  for (auto& h : module.deinit_hooks) {
    if (h) {
      checkStmt(*h);
    }
  }

  scopes_.clear();
  classes_.clear();
  interfaces_.clear();
  generic_funcs_.clear();
  free_funcs_.clear();
  active_generic_constraints_.clear();
  imported_modules_.clear();
  diags_ = nullptr;
  return diags.errorCount() == errors_before;
}

}  // namespace l::semantic
