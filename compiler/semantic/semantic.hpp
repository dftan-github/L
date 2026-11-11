#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "ast/ast.hpp"

namespace l::semantic {

class Analyzer {
public:
  // Mutability / sync data-race checks (and later: merge partial classes, resolve names).
  // Returns false if fatal diagnostics were emitted.
  bool analyze(ast::Module& module);

  const std::vector<std::string>& diagnostics() const { return diagnostics_; }

private:
  void error(const std::string& message);
  void checkStmt(const ast::Stmt& stmt);
  void checkStmtList(const std::vector<ast::StmtPtr>& stmts);
  void checkExpr(const ast::Expr& expr);

  bool in_sync_ = false;
  // Treat the whole program as a shared concurrent context for Phase-3 race checks:
  // any write to a `!` mutable binding outside `sync` is a fatal data race.
  std::unordered_set<std::string> mutable_vars_;
  std::vector<std::string> diagnostics_;
  bool had_error_ = false;
};

}  // namespace l::semantic
