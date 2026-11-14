#include "comptime/expand.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "comptime/interpreter.hpp"
#include "comptime/snippet.hpp"
#include "comptime/uid.hpp"

namespace l::comptime {
namespace {

constexpr const char* kHandlerName = "__compile_time_handler";

bool isClassHookToggle(const std::string& command) {
  return command == "enable_class_hook" || command == "disable_class_hook";
}

std::string runHandler(const ast::FuncDeclStmt& handler, const ast::DirectiveStmt& dir,
                       diag::DiagnosticEngine& diags) {
  Interpreter interp(&diags);
  // [source-line] [source-column] directive Token/AST loc carries exact line/column from Lexer.
  interp.setSourceContext(dir.loc);
  interp.setFuncContext(handler.name);

  std::vector<Value> args = {Value::makeString(dir.context_path), Value::makeString(dir.command),
                             Value::makeString(dir.args)};
  // Optional extended signature: (path, cmd, args, line, column).
  if (handler.params.size() == 5) {
    args.push_back(Value::makeInt(static_cast<std::int64_t>(dir.loc.line)));
    args.push_back(Value::makeInt(static_cast<std::int64_t>(dir.loc.column)));
  } else if (handler.params.size() != 3) {
    diags.error(dir.loc,
                "LCMD handler must be (String path, String cmd, String args) -> String "
                "or (…, i64 line, i64 column) -> String");
    return {};
  }

  Value result = interp.callFunction(handler, args);
  if (result.kind == ValueKind::String) {
    return result.s;
  }
  if (result.kind == ValueKind::Nil) {
    return {};
  }
  diags.error(dir.loc, "LCMD handler must return String");
  return {};
}

// Inject snippet stmts into a class (methods/fields) or into `rebuilt` (statement scope).
bool absorbInjected(std::vector<ast::StmtPtr>& pieces, ast::ClassDeclStmt* enclosing_class,
                    std::vector<ast::StmtPtr>& rebuilt, diag::DiagnosticEngine& diags,
                    const ast::SourceLocation& loc) {
  for (ast::StmtPtr& p : pieces) {
    if (!p) {
      continue;
    }
    if (enclosing_class && p->kind() == ast::StmtKind::FuncDecl) {
      auto* raw = static_cast<ast::FuncDeclStmt*>(p.release());
      raw->is_method = true;
      enclosing_class->methods.push_back(std::unique_ptr<ast::FuncDeclStmt>(raw));
      continue;
    }
    if (enclosing_class && p->kind() == ast::StmtKind::FieldDecl) {
      auto* raw = static_cast<ast::FieldDeclStmt*>(p.release());
      enclosing_class->fields.push_back(std::unique_ptr<ast::FieldDeclStmt>(raw));
      continue;
    }
    if (enclosing_class) {
      diags.error(loc, "LCMD injection inside class context must be a field or method");
      return false;
    }
    rebuilt.push_back(std::move(p));
  }
  return true;
}

bool expandStmtList(const ast::FuncDeclStmt* handler, std::vector<ast::StmtPtr>& stmts,
                    diag::DiagnosticEngine& diags, ast::ClassDeclStmt* enclosing_class) {
  std::vector<ast::StmtPtr> rebuilt;
  rebuilt.reserve(stmts.size());
  bool ok = true;

  for (ast::StmtPtr& stmt : stmts) {
    if (!stmt) {
      continue;
    }

    switch (stmt->kind()) {
      case ast::StmtKind::FuncDecl: {
        auto& fn = static_cast<ast::FuncDeclStmt&>(*stmt);
        if (!expandStmtList(handler, fn.body, diags, enclosing_class)) {
          ok = false;
        }
        break;
      }
      case ast::StmtKind::If: {
        auto& ifs = static_cast<ast::IfStmt&>(*stmt);
        if (!expandStmtList(handler, ifs.then_body, diags, enclosing_class) ||
            !expandStmtList(handler, ifs.else_body, diags, enclosing_class)) {
          ok = false;
        }
        break;
      }
      case ast::StmtKind::While: {
        auto& w = static_cast<ast::WhileStmt&>(*stmt);
        if (!expandStmtList(handler, w.body, diags, enclosing_class)) {
          ok = false;
        }
        break;
      }
      case ast::StmtKind::ForIn: {
        auto& f = static_cast<ast::ForInStmt&>(*stmt);
        if (!expandStmtList(handler, f.body, diags, enclosing_class)) {
          ok = false;
        }
        break;
      }
      case ast::StmtKind::Match: {
        auto& m = static_cast<ast::MatchStmt&>(*stmt);
        for (ast::MatchBranch& br : m.branches) {
          if (!expandStmtList(handler, br.body, diags, enclosing_class)) {
            ok = false;
          }
        }
        break;
      }
      case ast::StmtKind::Defer: {
        auto& d = static_cast<ast::DeferStmt&>(*stmt);
        if (!expandStmtList(handler, d.body, diags, enclosing_class)) {
          ok = false;
        }
        break;
      }
      case ast::StmtKind::Block: {
        auto& b = static_cast<ast::BlockStmt&>(*stmt);
        if (!expandStmtList(handler, b.stmts, diags, enclosing_class)) {
          ok = false;
        }
        break;
      }
      case ast::StmtKind::Sync: {
        auto& s = static_cast<ast::SyncBlockStmt&>(*stmt);
        if (!expandStmtList(handler, s.body, diags, enclosing_class)) {
          ok = false;
        }
        break;
      }
      case ast::StmtKind::Spawn: {
        auto& s = static_cast<ast::SpawnBlockStmt&>(*stmt);
        if (!expandStmtList(handler, s.body, diags, enclosing_class)) {
          ok = false;
        }
        break;
      }
      case ast::StmtKind::Comptime: {
        auto& ct = static_cast<ast::ComptimeStmt&>(*stmt);
        if (!expandStmtList(handler, ct.body, diags, enclosing_class)) {
          ok = false;
        }
        break;
      }
      case ast::StmtKind::ClassDecl: {
        auto& cls = static_cast<ast::ClassDeclStmt&>(*stmt);
        // Index by size snapshot: LCMD may push new methods onto cls.methods.
        const std::size_t method_count = cls.methods.size();
        for (std::size_t i = 0; i < method_count; ++i) {
          if (cls.methods[i] &&
              !expandStmtList(handler, cls.methods[i]->body, diags, &cls)) {
            ok = false;
          }
        }
        for (std::size_t i = method_count; i < cls.methods.size(); ++i) {
          if (cls.methods[i] &&
              !expandStmtList(handler, cls.methods[i]->body, diags, &cls)) {
            ok = false;
          }
        }
        break;
      }
      default:
        break;
    }

    if (stmt->kind() == ast::StmtKind::Directive) {
      const auto& dir = static_cast<const ast::DirectiveStmt&>(*stmt);
      // enable/disable were applied during parse; drop with zero interpreter cost.
      if (isClassHookToggle(dir.command)) {
        continue;
      }
      if (!handler) {
        diags.error(dir.loc, std::string("LCMD requires pub fun ") + kHandlerName +
                                 "(String path, String cmd, String args) -> String");
        ok = false;
        continue;
      }
      const std::string injected = runHandler(*handler, dir, diags);
      if (diags.hasErrors()) {
        ok = false;
        continue;
      }
      std::vector<ast::StmtPtr> pieces = parseSnippet(injected, dir.loc, diags);
      if (diags.hasErrors()) {
        ok = false;
        continue;
      }
      if (!expandStmtList(handler, pieces, diags, enclosing_class)) {
        ok = false;
      }
      if (!absorbInjected(pieces, enclosing_class, rebuilt, diags, dir.loc)) {
        ok = false;
      }
      continue;
    }

    // Execute `comptime { ... }` then strip — never reaches codegen/LLVM.
    if (stmt->kind() == ast::StmtKind::Comptime) {
      const auto& ct = static_cast<const ast::ComptimeStmt&>(*stmt);
      Interpreter interp(&diags);
      interp.setSourceContext(ct.loc);
      interp.setFuncContext("<comptime>");
      if (!interp.execStmts(ct.body)) {
        ok = false;
      }
      continue;
    }

    rebuilt.push_back(std::move(stmt));
  }

  stmts = std::move(rebuilt);
  return ok;
}

}  // namespace

bool expandLcmdDirectives(ast::Module& module, diag::DiagnosticEngine& diags) {
  const ast::FuncDeclStmt* handler = Interpreter::findFunction(module, kHandlerName);
  return expandStmtList(handler, module.stmts, diags, /*enclosing_class=*/nullptr);
}

}  // namespace l::comptime
