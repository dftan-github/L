#include "codegen/codegen.hpp"

#include <iostream>
#include <utility>

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

namespace l::codegen {
namespace {

void ensureNativeTargets() {
  static bool once = false;
  if (once) {
    return;
  }
  once = true;
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();
}

}  // namespace

llvm::LLVMContext& globalContext() {
  static llvm::LLVMContext context;
  return context;
}

Codegen::Codegen(std::string module_name)
    : module_(std::make_unique<llvm::Module>(std::move(module_name), globalContext())),
      builder_(globalContext()) {}

llvm::Type* Codegen::i32Ty() {
  return llvm::Type::getInt32Ty(globalContext());
}

llvm::Type* Codegen::i1Ty() {
  return llvm::Type::getInt1Ty(globalContext());
}

llvm::Type* Codegen::voidTy() {
  return llvm::Type::getVoidTy(globalContext());
}

llvm::Type* Codegen::typeFromName(const std::string& name) {
  if (name == "void") {
    return voidTy();
  }
  return i32Ty();
}

llvm::AllocaInst* Codegen::createEntryAlloca(llvm::Function* fn, llvm::Type* ty,
                                             const std::string& name) {
  llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
  return tmp.CreateAlloca(ty, nullptr, name);
}

llvm::Function* Codegen::getOrDeclarePrintInt() {
  if (llvm::Function* existing = module_->getFunction("l_print_int")) {
    return existing;
  }
  auto* fty = llvm::FunctionType::get(voidTy(), {i32Ty()}, false);
  return llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "l_print_int",
                                module_.get());
}

void Codegen::error(const std::string& message) {
  had_error_ = true;
  std::cerr << "codegen error: " << message << '\n';
}

llvm::Value* Codegen::toI1(llvm::Value* v) {
  if (!v) {
    return nullptr;
  }
  if (v->getType()->isIntegerTy(1)) {
    return v;
  }
  return builder_.CreateICmpNE(v, llvm::ConstantInt::get(v->getType(), 0), "tobool");
}

llvm::Value* Codegen::toI32(llvm::Value* v) {
  if (!v) {
    return nullptr;
  }
  if (v->getType()->isIntegerTy(32)) {
    return v;
  }
  if (v->getType()->isIntegerTy(1)) {
    return builder_.CreateZExt(v, i32Ty(), "booltmp");
  }
  return builder_.CreateIntCast(v, i32Ty(), /*isSigned=*/true, "casttmp");
}

void Codegen::branchTo(llvm::BasicBlock* bb) {
  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(bb);
  }
}

bool Codegen::emitStmtList(const std::vector<ast::StmtPtr>& stmts) {
  for (const ast::StmtPtr& stmt : stmts) {
    if (!stmt) {
      continue;
    }
    if (!emitStmt(*stmt)) {
      return false;
    }
    if (builder_.GetInsertBlock() && builder_.GetInsertBlock()->getTerminator()) {
      break;
    }
  }
  return true;
}

bool Codegen::emit(const ast::Module& module) {
  had_error_ = false;
  named_values_.clear();

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::FuncDecl) {
      continue;
    }
    const auto& fn = static_cast<const ast::FuncDeclStmt&>(*stmt);
    std::vector<llvm::Type*> params(fn.params.size(), i32Ty());
    llvm::Type* ret_ty = typeFromName(fn.return_type);
    auto* fty = llvm::FunctionType::get(ret_ty, params, false);
    llvm::Function::Create(fty, llvm::Function::ExternalLinkage, fn.name, module_.get());
  }

  getOrDeclarePrintInt();

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    if (!emitStmt(*stmt)) {
      return false;
    }
  }

  if (llvm::verifyModule(*module_, &llvm::errs())) {
    error("module verification failed");
    return false;
  }
  return !had_error_;
}

bool Codegen::emitObjectFile(const std::string& object_path) {
  ensureNativeTargets();

  const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  module_->setTargetTriple(triple);

  std::string err;
  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
  if (!target) {
    error("lookupTarget failed: " + err);
    return false;
  }

  llvm::TargetOptions opt;
  auto reloc = std::optional<llvm::Reloc::Model>();
  std::unique_ptr<llvm::TargetMachine> tm(
      target->createTargetMachine(triple, "generic", "", opt, reloc));
  if (!tm) {
    error("failed to create TargetMachine");
    return false;
  }

  module_->setDataLayout(tm->createDataLayout());

  std::error_code ec;
  llvm::raw_fd_ostream dest(object_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    error("could not open object file '" + object_path + "': " + ec.message());
    return false;
  }

  llvm::legacy::PassManager pass;
  if (tm->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    error("TargetMachine cannot emit object file");
    return false;
  }
  pass.run(*module_);
  dest.flush();
  return true;
}

bool Codegen::emitStmt(const ast::Stmt& stmt) {
  switch (stmt.kind()) {
    case ast::StmtKind::FuncDecl:
      return emitFuncDecl(static_cast<const ast::FuncDeclStmt&>(stmt));
    case ast::StmtKind::VarDecl:
      return emitVarDecl(static_cast<const ast::VarDeclStmt&>(stmt));
    case ast::StmtKind::Assign:
      return emitAssign(static_cast<const ast::AssignStmt&>(stmt));
    case ast::StmtKind::Return:
      return emitReturn(static_cast<const ast::ReturnStmt&>(stmt));
    case ast::StmtKind::ExprStmt:
      return emitExprStmt(static_cast<const ast::ExprStmt&>(stmt));
    case ast::StmtKind::If:
      return emitIf(static_cast<const ast::IfStmt&>(stmt));
    case ast::StmtKind::While:
      return emitWhile(static_cast<const ast::WhileStmt&>(stmt));
  }
  error("unsupported statement in codegen");
  return false;
}

bool Codegen::emitFuncDecl(const ast::FuncDeclStmt& fn_ast) {
  llvm::Function* fn = module_->getFunction(fn_ast.name);
  if (!fn) {
    error("missing function declaration: " + fn_ast.name);
    return false;
  }
  if (!fn->empty()) {
    error("redefinition of function: " + fn_ast.name);
    return false;
  }

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  named_values_.clear();

  unsigned idx = 0;
  for (auto& arg : fn->args()) {
    const ast::Param& param = fn_ast.params[idx];
    arg.setName(param.name);
    llvm::AllocaInst* alloca = createEntryAlloca(fn, i32Ty(), param.name);
    builder_.CreateStore(&arg, alloca);
    named_values_[param.name] = alloca;
    ++idx;
  }

  if (!emitStmtList(fn_ast.body)) {
    return false;
  }

  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    if (fn->getReturnType()->isVoidTy()) {
      builder_.CreateRetVoid();
    } else {
      builder_.CreateRet(llvm::ConstantInt::get(i32Ty(), 0));
    }
  }

  if (llvm::verifyFunction(*fn, &llvm::errs())) {
    error("function verification failed: " + fn_ast.name);
    fn->eraseFromParent();
    return false;
  }
  return true;
}

bool Codegen::emitVarDecl(const ast::VarDeclStmt& decl) {
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* alloca = createEntryAlloca(fn, i32Ty(), decl.name);
  llvm::Value* init = decl.init ? toI32(emitExpr(*decl.init))
                                : llvm::ConstantInt::get(i32Ty(), 0);
  if (!init) {
    return false;
  }
  builder_.CreateStore(init, alloca);
  named_values_[decl.name] = alloca;
  return true;
}

bool Codegen::emitAssign(const ast::AssignStmt& assign) {
  auto it = named_values_.find(assign.name);
  if (it == named_values_.end()) {
    error("assignment to unknown variable: " + assign.name);
    return false;
  }
  llvm::Value* v = toI32(emitExpr(*assign.value));
  if (!v) {
    return false;
  }
  builder_.CreateStore(v, it->second);
  return true;
}

bool Codegen::emitReturn(const ast::ReturnStmt& ret) {
  if (!ret.value) {
    builder_.CreateRetVoid();
    return true;
  }
  llvm::Value* v = toI32(emitExpr(*ret.value));
  if (!v) {
    return false;
  }
  builder_.CreateRet(v);
  return true;
}

bool Codegen::emitExprStmt(const ast::ExprStmt& stmt) {
  if (!stmt.expr) {
    return true;
  }
  return emitExpr(*stmt.expr) != nullptr;
}

bool Codegen::emitIf(const ast::IfStmt& stmt) {
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::Value* cond = toI1(emitExpr(*stmt.cond));
  if (!cond) {
    return false;
  }

  llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(globalContext(), "if.then", fn);
  llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(globalContext(), "if.else", fn);
  llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(globalContext(), "if.merge", fn);

  builder_.CreateCondBr(cond, then_bb, else_bb);

  // Locals live in allocas; MergeBB continues after both sides store. Loads after
  // merge see memory SSA values (equivalent to PHI of the stored values).
  builder_.SetInsertPoint(then_bb);
  if (!emitStmtList(stmt.then_body)) {
    return false;
  }
  branchTo(merge_bb);

  builder_.SetInsertPoint(else_bb);
  if (!emitStmtList(stmt.else_body)) {
    return false;
  }
  branchTo(merge_bb);

  builder_.SetInsertPoint(merge_bb);
  return true;
}

bool Codegen::emitWhile(const ast::WhileStmt& stmt) {
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(globalContext(), "while.cond", fn);
  llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(globalContext(), "while.body", fn);
  llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(globalContext(), "while.end", fn);

  branchTo(cond_bb);
  builder_.SetInsertPoint(cond_bb);
  llvm::Value* cond = toI1(emitExpr(*stmt.cond));
  if (!cond) {
    return false;
  }
  builder_.CreateCondBr(cond, body_bb, end_bb);

  builder_.SetInsertPoint(body_bb);
  if (!emitStmtList(stmt.body)) {
    return false;
  }
  branchTo(cond_bb);

  builder_.SetInsertPoint(end_bb);
  return true;
}

llvm::Value* Codegen::emitExpr(const ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::IntLiteral: {
      const auto& lit = static_cast<const ast::IntLiteralExpr&>(expr);
      long long n = 0;
      try {
        n = std::stoll(lit.value);
      } catch (...) {
        error("invalid integer literal: " + lit.value);
        return nullptr;
      }
      return llvm::ConstantInt::get(i32Ty(), n, true);
    }
    case ast::ExprKind::BoolLiteral: {
      const auto& lit = static_cast<const ast::BooleanLiteralExpr&>(expr);
      return lit.value ? llvm::ConstantInt::getTrue(globalContext())
                       : llvm::ConstantInt::getFalse(globalContext());
    }
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      auto it = named_values_.find(id.name);
      if (it == named_values_.end()) {
        error("unknown variable: " + id.name);
        return nullptr;
      }
      return builder_.CreateLoad(i32Ty(), it->second, id.name);
    }
    case ast::ExprKind::Binary: {
      const auto& bin = static_cast<const ast::BinaryExpr&>(expr);

      // Short-circuit && / ||
      if (bin.op == "&&" || bin.op == "||") {
        llvm::Function* fn = builder_.GetInsertBlock()->getParent();
        llvm::Value* lhs = toI1(emitExpr(*bin.left));
        if (!lhs) {
          return nullptr;
        }
        llvm::BasicBlock* rhs_bb = llvm::BasicBlock::Create(globalContext(), "logic.rhs", fn);
        llvm::BasicBlock* merge_bb =
            llvm::BasicBlock::Create(globalContext(), "logic.merge", fn);
        llvm::BasicBlock* start_bb = builder_.GetInsertBlock();

        if (bin.op == "&&") {
          builder_.CreateCondBr(lhs, rhs_bb, merge_bb);
        } else {
          builder_.CreateCondBr(lhs, merge_bb, rhs_bb);
        }

        builder_.SetInsertPoint(rhs_bb);
        llvm::Value* rhs = toI1(emitExpr(*bin.right));
        if (!rhs) {
          return nullptr;
        }
        llvm::BasicBlock* rhs_end = builder_.GetInsertBlock();
        builder_.CreateBr(merge_bb);

        builder_.SetInsertPoint(merge_bb);
        llvm::PHINode* phi = builder_.CreatePHI(i1Ty(), 2, "logictmp");
        if (bin.op == "&&") {
          phi->addIncoming(llvm::ConstantInt::getFalse(globalContext()), start_bb);
          phi->addIncoming(rhs, rhs_end);
        } else {
          phi->addIncoming(llvm::ConstantInt::getTrue(globalContext()), start_bb);
          phi->addIncoming(rhs, rhs_end);
        }
        return phi;
      }

      llvm::Value* lhs = emitExpr(*bin.left);
      llvm::Value* rhs = emitExpr(*bin.right);
      if (!lhs || !rhs) {
        return nullptr;
      }

      if (bin.op == "+" || bin.op == "-" || bin.op == "*" || bin.op == "/") {
        lhs = toI32(lhs);
        rhs = toI32(rhs);
        if (bin.op == "+") return builder_.CreateAdd(lhs, rhs, "addtmp");
        if (bin.op == "-") return builder_.CreateSub(lhs, rhs, "subtmp");
        if (bin.op == "*") return builder_.CreateMul(lhs, rhs, "multmp");
        return builder_.CreateSDiv(lhs, rhs, "divtmp");
      }

      lhs = toI32(lhs);
      rhs = toI32(rhs);
      if (bin.op == "==") return builder_.CreateICmpEQ(lhs, rhs, "eqtmp");
      if (bin.op == "!=") return builder_.CreateICmpNE(lhs, rhs, "netmp");
      if (bin.op == "<") return builder_.CreateICmpSLT(lhs, rhs, "lttmp");
      if (bin.op == ">") return builder_.CreateICmpSGT(lhs, rhs, "gttmp");
      if (bin.op == "<=") return builder_.CreateICmpSLE(lhs, rhs, "letmp");
      if (bin.op == ">=") return builder_.CreateICmpSGE(lhs, rhs, "getmp");

      error("unsupported binary operator: " + bin.op);
      return nullptr;
    }
    case ast::ExprKind::Call: {
      const auto& call = static_cast<const ast::CallExpr&>(expr);
      if (call.callee == "print") {
        if (call.args.size() != 1) {
          error("print expects exactly 1 argument");
          return nullptr;
        }
        llvm::Value* arg = toI32(emitExpr(*call.args[0]));
        if (!arg) {
          return nullptr;
        }
        builder_.CreateCall(getOrDeclarePrintInt(), {arg});
        return llvm::ConstantInt::get(i32Ty(), 0);
      }

      llvm::Function* callee = module_->getFunction(call.callee);
      if (!callee) {
        error("unknown function: " + call.callee);
        return nullptr;
      }
      if (callee->arg_size() != call.args.size()) {
        error("argument count mismatch calling " + call.callee);
        return nullptr;
      }
      std::vector<llvm::Value*> args;
      for (const ast::ExprPtr& arg : call.args) {
        llvm::Value* v = toI32(emitExpr(*arg));
        if (!v) {
          return nullptr;
        }
        args.push_back(v);
      }
      if (callee->getReturnType()->isVoidTy()) {
        builder_.CreateCall(callee, args);
        return llvm::ConstantInt::get(i32Ty(), 0);
      }
      return builder_.CreateCall(callee, args, "calltmp");
    }
  }
  error("unsupported expression in codegen");
  return nullptr;
}

}  // namespace l::codegen
