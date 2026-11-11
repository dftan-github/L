#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
  struct InterfaceInfo {
    std::vector<std::string> method_names;
    std::unordered_map<std::string, unsigned> method_index;
    std::unordered_map<std::string, const ast::FuncDeclStmt*> methods;
  };

  struct ClassInfo {
    llvm::StructType* type = nullptr;
    std::unordered_map<std::string, unsigned> field_index;
    std::vector<std::string> field_types;
    std::vector<std::string> interfaces;
    std::unordered_map<std::string, llvm::Function*> methods;
    std::unordered_map<std::string, llvm::GlobalVariable*> vtables;
    const ast::ClassDeclStmt* ast = nullptr;
    // For specialized generic classes (e.g. Array<i32>): concrete type args.
    std::vector<std::string> type_args;
    std::string template_name;  // "Array" when name is "Array<i32>"
    std::string ir_name;        // LLVM-friendly name (Array_i32)
  };

  llvm::Type* i32Ty();
  llvm::Type* i64Ty();
  llvm::Type* i1Ty();
  llvm::Type* voidTy();
  llvm::Type* ptrTy();
  llvm::StructType* fatPtrTy();
  llvm::Type* typeFromName(const std::string& name);
  std::string resolveTypeName(const std::string& name) const;

  bool isClassType(const std::string& name) const;
  bool isInterfaceType(const std::string& name) const;
  bool classImplements(const std::string& class_name, const std::string& iface) const;
  const ClassInfo* findClass(const std::string& name) const;
  ClassInfo* findClassMut(const std::string& name);
  const InterfaceInfo* findInterface(const std::string& name) const;

  llvm::AllocaInst* createEntryAlloca(llvm::Function* fn, llvm::Type* ty,
                                      const std::string& name);
  llvm::Function* getOrDeclarePrintInt();
  llvm::Function* getOrDeclarePrintStr();
  llvm::Function* getOrDeclareAlloc();
  llvm::Function* getOrDeclareRealloc();
  llvm::Function* getOrDeclareFree();
  llvm::Function* getOrDeclareStrConcat();
  llvm::Function* getOrDeclareReadLine();
  llvm::Type* llvmTypeForTypeName(const std::string& name);
  // Best-effort expression type for operator lowering / := inference.
  std::string exprTypeOf(const ast::Expr& expr) const;
  bool isStringOperand(const ast::Expr& expr) const;
  // Materialize a String object pointer (alloca / class ptr) for concat / print.
  llvm::Value* emitStringObjectPtr(const ast::Expr& expr);
  // Materialize a String by-value for concat RHS / returns.
  llvm::Value* emitStringByValue(const ast::Expr& expr);
  llvm::Value* emitStringAdd(const ast::BinaryExpr& bin);
  bool initClassAtPtr(llvm::Value* base, const ClassInfo& info, const ast::NewExpr& neu);
  bool initClassFromNew(llvm::AllocaInst* alloca, const ClassInfo& info,
                        const ast::NewExpr& neu);
  llvm::Value* classObjectPtr(const std::string& name);

  static bool splitGenericType(const std::string& name, std::string& base,
                               std::vector<std::string>& args);
  static std::string mangleSpecName(const std::string& base,
                                    const std::vector<std::string>& args);
  bool ensureTypeAvailable(const std::string& name);
  bool specializeClass(const ast::ClassDeclStmt& tmpl, const std::vector<std::string>& args,
                       const std::string& instance_name);
  uint64_t allocSizeOfTypeName(const std::string& type_name);
  llvm::Type* elementTypeForPtrIndex() const;
  llvm::Value* emitIndexLValue(const ast::IndexExpr& expr);
  llvm::Value* emitSizeof(const ast::SizeofExpr& expr);

  llvm::Value* toI1(llvm::Value* v);
  llvm::Value* toI32(llvm::Value* v);
  bool emitStmtList(const std::vector<ast::StmtPtr>& stmts);
  bool emitStmtListScoped(const std::vector<ast::StmtPtr>& stmts);
  void branchTo(llvm::BasicBlock* bb);

  void enterDeferScope();
  void exitDeferScope();
  bool flushCurrentScopeDefers();
  bool flushAllDefers();

  bool emitStmt(const ast::Stmt& stmt);
  llvm::Value* emitExpr(const ast::Expr& expr);
  llvm::Value* emitLValue(const ast::Expr& expr);
  llvm::Value* emitHeapExpr(const ast::HeapExpr& expr);

  bool registerInterface(const ast::InterfaceDeclStmt& decl);
  bool emitClassStruct(const ast::ClassDeclStmt& decl);
  bool declareClassMethods(const ast::ClassDeclStmt& decl);
  bool buildVtables(const ast::ClassDeclStmt& decl);
  bool emitClassMethodBodies(const ast::ClassDeclStmt& decl);
  bool emitMethodBody(const std::string& class_name, const ast::FuncDeclStmt& method);

  bool emitFuncDecl(const ast::FuncDeclStmt& fn);
  bool emitSpecializedFunc(const ast::FuncDeclStmt& tmpl,
                           const std::string& mangled,
                           const std::unordered_map<std::string, std::string>& subst);
  llvm::Function* ensureSpecialized(const ast::FuncDeclStmt& tmpl,
                                    const std::vector<std::string>& arg_type_names);
  llvm::Value* emitGenericOrPlainCall(const ast::CallExpr& call);
  // Reorder positional+keyword CallArgs to match FuncDecl param order.
  bool resolveCallArgs(const ast::FuncDeclStmt& fn, const std::vector<ast::CallArg>& raw,
                       std::vector<const ast::Expr*>& ordered);
  llvm::Value* emitValueForParam(const ast::Expr& expr, llvm::Type* expected,
                                 const std::string& expected_type_name);

  bool emitVarDecl(const ast::VarDeclStmt& decl);
  // Infer local type from RHS for `name := expr` (NewExpr / heap New / literals).
  std::string inferTypeFromExpr(const ast::Expr& expr);
  bool emitAssign(const ast::AssignStmt& assign);
  bool emitReturn(const ast::ReturnStmt& ret);
  bool emitExprStmt(const ast::ExprStmt& stmt);
  bool emitIf(const ast::IfStmt& stmt);
  bool emitWhile(const ast::WhileStmt& stmt);
  bool emitForIn(const ast::ForInStmt& stmt);
  bool emitDefer(const ast::DeferStmt& stmt);
  bool emitSync(const ast::SyncBlockStmt& stmt);

  llvm::Value* boxToInterface(llvm::Value* class_ptr, const std::string& class_name,
                              const std::string& iface_name);
  llvm::Value* emitMethodCall(const ast::MethodCallExpr& call);

  void error(const std::string& message);
  void ensureDataLayout();

  static std::string mangleName(const std::string& module_name, const std::string& name,
                                bool is_pub);
  std::string resolveFuncIrName(const std::string& callee) const;

  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> builder_;
  std::unordered_map<std::string, llvm::AllocaInst*> named_values_;
  std::unordered_map<std::string, std::string> local_types_;
  std::unordered_set<std::string> heap_locals_;  // class locals stored as ptr alloca
  std::unordered_map<std::string, ClassInfo> classes_;
  std::unordered_map<std::string, InterfaceInfo> interfaces_;
  std::unordered_map<std::string, const ast::FuncDeclStmt*> generic_templates_;
  std::unordered_map<std::string, const ast::FuncDeclStmt*> concrete_funcs_;  // for KW reorder
  std::unordered_map<std::string, const ast::ClassDeclStmt*> generic_class_templates_;
  std::unordered_map<std::string, llvm::Function*> specialized_funcs_;
  std::unordered_map<std::string, std::string> type_subst_;
  // Cross-module symbol tables: short name → LLVM IR name.
  std::unordered_map<std::string, std::string> public_funcs_;
  std::unordered_map<std::string, std::string> private_funcs_;  // "mod::name" → IR
  std::string current_module_;
  llvm::StructType* fat_ptr_ty_ = nullptr;

  // Defer scopes: outer -> inner. Each scope holds DeferStmt* in registration order.
  std::vector<std::vector<const ast::DeferStmt*>> defer_scopes_;

  std::string current_class_;
  llvm::Value* current_this_ = nullptr;

  bool had_error_ = false;
};

}  // namespace l::codegen
