#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/ast.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

namespace l::codegen {

llvm::LLVMContext& globalContext();

class Codegen {
public:
  explicit Codegen(std::string module_name);

  bool emit(const ast::Module& module);
  bool emitObjectFile(const std::string& object_path);

  llvm::Module& module() { return *module_; }
  const llvm::Module& module() const { return *module_; }

private:
  llvm::Type* i32Ty();
  llvm::Type* i1Ty();
  llvm::Type* voidTy();
  llvm::Type* typeFromName(const std::string& name);
  llvm::AllocaInst* createEntryAlloca(llvm::Function* fn, llvm::Type* ty,
                                      const std::string& name);
  llvm::Function* getOrDeclarePrintInt();

  llvm::Value* toI1(llvm::Value* v);
  llvm::Value* toI32(llvm::Value* v);
  bool emitStmtList(const std::vector<ast::StmtPtr>& stmts);
  void branchTo(llvm::BasicBlock* bb);

  bool emitStmt(const ast::Stmt& stmt);
  llvm::Value* emitExpr(const ast::Expr& expr);

  bool emitFuncDecl(const ast::FuncDeclStmt& fn);
  bool emitVarDecl(const ast::VarDeclStmt& decl);
  bool emitAssign(const ast::AssignStmt& assign);
  bool emitReturn(const ast::ReturnStmt& ret);
  bool emitExprStmt(const ast::ExprStmt& stmt);
  bool emitIf(const ast::IfStmt& stmt);
  bool emitWhile(const ast::WhileStmt& stmt);

  void error(const std::string& message);

  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> builder_;
  std::unordered_map<std::string, llvm::AllocaInst*> named_values_;
  bool had_error_ = false;
};

}  // namespace l::codegen
