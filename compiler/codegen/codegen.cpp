#include "codegen/codegen.hpp"

#include <iostream>
#include <iterator>
#include <utility>

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
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

#include <algorithm>

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

std::string mangleMethod(const std::string& class_name, const std::string& method) {
  return class_name + "_" + method;
}

std::string vtableName(const std::string& class_name, const std::string& iface) {
  return "vtable_" + class_name + "_" + iface;
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

llvm::Type* Codegen::i64Ty() {
  return llvm::Type::getInt64Ty(globalContext());
}

llvm::Type* Codegen::i1Ty() {
  return llvm::Type::getInt1Ty(globalContext());
}

llvm::Type* Codegen::voidTy() {
  return llvm::Type::getVoidTy(globalContext());
}

llvm::Type* Codegen::ptrTy() {
  return llvm::PointerType::get(globalContext(), /*AddressSpace=*/0);
}

llvm::StructType* Codegen::fatPtrTy() {
  if (!fat_ptr_ty_) {
    // { data_ptr, vtable_ptr } — naturally aligned dual-pointer fat pointer.
    fat_ptr_ty_ = llvm::StructType::create(globalContext(), {ptrTy(), ptrTy()}, "l.FatPtr");
  }
  return fat_ptr_ty_;
}

bool Codegen::isClassType(const std::string& name) const {
  return classes_.find(name) != classes_.end();
}

bool Codegen::isInterfaceType(const std::string& name) const {
  return interfaces_.find(name) != interfaces_.end();
}

const Codegen::ClassInfo* Codegen::findClass(const std::string& name) const {
  auto it = classes_.find(name);
  if (it == classes_.end()) {
    return nullptr;
  }
  return &it->second;
}

Codegen::ClassInfo* Codegen::findClassMut(const std::string& name) {
  auto it = classes_.find(name);
  if (it == classes_.end()) {
    return nullptr;
  }
  return &it->second;
}

const Codegen::InterfaceInfo* Codegen::findInterface(const std::string& name) const {
  auto it = interfaces_.find(name);
  if (it == interfaces_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::string Codegen::resolveTypeName(const std::string& name) const {
  auto it = type_subst_.find(name);
  if (it != type_subst_.end()) {
    return it->second;
  }
  return name;
}

llvm::Type* Codegen::llvmTypeForTypeName(const std::string& name) {
  const std::string resolved = resolveTypeName(name);
  if (resolved == "void") {
    return voidTy();
  }
  if (resolved == "ptr") {
    return ptrTy();
  }
  if (resolved == "bool") {
    return i1Ty();
  }
  if (resolved == "i64") {
    return llvm::Type::getInt64Ty(globalContext());
  }
  if (resolved == "f64") {
    return llvm::Type::getDoubleTy(globalContext());
  }
  if (isInterfaceType(resolved)) {
    return fatPtrTy();
  }
  if (const ClassInfo* info = findClass(resolved)) {
    return info->type;
  }
  return i32Ty();
}

llvm::Type* Codegen::typeFromName(const std::string& name) {
  return llvmTypeForTypeName(name);
}

bool Codegen::classImplements(const std::string& class_name, const std::string& iface) const {
  const ClassInfo* info = findClass(class_name);
  if (!info) {
    return false;
  }
  for (const std::string& i : info->interfaces) {
    if (i == iface) {
      return true;
    }
  }
  return false;
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

llvm::Function* Codegen::getOrDeclarePrintStr() {
  if (llvm::Function* existing = module_->getFunction("l_print_str")) {
    return existing;
  }
  auto* fty = llvm::FunctionType::get(voidTy(), {ptrTy()}, false);
  return llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "l_print_str",
                                module_.get());
}

llvm::Function* Codegen::getOrDeclareAlloc() {
  if (llvm::Function* existing = module_->getFunction("l_alloc")) {
    return existing;
  }
  auto* fty = llvm::FunctionType::get(ptrTy(), {i64Ty()}, false);
  return llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "l_alloc",
                                module_.get());
}

llvm::Function* Codegen::getOrDeclareFree() {
  if (llvm::Function* existing = module_->getFunction("l_free")) {
    return existing;
  }
  auto* fty = llvm::FunctionType::get(voidTy(), {ptrTy()}, false);
  return llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "l_free",
                                module_.get());
}

void Codegen::ensureDataLayout() {
  if (!module_->getDataLayout().getStringRepresentation().empty()) {
    return;
  }
  ensureNativeTargets();
  const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  std::string err;
  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
  if (!target) {
    return;
  }
  llvm::TargetOptions opt;
  auto reloc = std::optional<llvm::Reloc::Model>();
  std::unique_ptr<llvm::TargetMachine> tm(
      target->createTargetMachine(triple, "generic", "", opt, reloc));
  if (tm) {
    module_->setDataLayout(tm->createDataLayout());
    module_->setTargetTriple(triple);
  }
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

void Codegen::enterDeferScope() {
  defer_scopes_.emplace_back();
}

bool Codegen::flushCurrentScopeDefers() {
  if (defer_scopes_.empty()) {
    return true;
  }
  auto& scope = defer_scopes_.back();
  for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
    const ast::DeferStmt* def = *it;
    if (!def) {
      continue;
    }
    for (const ast::StmtPtr& s : def->body) {
      if (!s) {
        continue;
      }
      if (!emitStmt(*s)) {
        return false;
      }
    }
  }
  scope.clear();
  return true;
}

void Codegen::exitDeferScope() {
  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    flushCurrentScopeDefers();
  } else if (!defer_scopes_.empty()) {
    defer_scopes_.back().clear();
  }
  if (!defer_scopes_.empty()) {
    defer_scopes_.pop_back();
  }
}

bool Codegen::flushAllDefers() {
  for (auto sit = defer_scopes_.rbegin(); sit != defer_scopes_.rend(); ++sit) {
    for (auto dit = sit->rbegin(); dit != sit->rend(); ++dit) {
      const ast::DeferStmt* def = *dit;
      if (!def) {
        continue;
      }
      for (const ast::StmtPtr& s : def->body) {
        if (!s) {
          continue;
        }
        if (!emitStmt(*s)) {
          return false;
        }
      }
    }
    sit->clear();
  }
  return true;
}

bool Codegen::emitStmtListScoped(const std::vector<ast::StmtPtr>& stmts) {
  enterDeferScope();
  const bool ok = emitStmtList(stmts);
  if (ok) {
    exitDeferScope();
  } else if (!defer_scopes_.empty()) {
    defer_scopes_.pop_back();
  }
  return ok;
}

llvm::Value* Codegen::classObjectPtr(const std::string& name) {
  auto it = named_values_.find(name);
  if (it == named_values_.end()) {
    error("unknown variable: " + name);
    return nullptr;
  }
  if (heap_locals_.count(name) != 0) {
    return builder_.CreateLoad(ptrTy(), it->second, name + ".obj");
  }
  return it->second;
}

bool Codegen::registerInterface(const ast::InterfaceDeclStmt& decl) {
  if (interfaces_.count(decl.name) != 0) {
    error("redefinition of interface: " + decl.name);
    return false;
  }
  InterfaceInfo info;
  unsigned idx = 0;
  for (const auto& method : decl.methods) {
    if (!method) {
      continue;
    }
    if (info.method_index.count(method->name) != 0) {
      error("duplicate method in interface " + decl.name + ": " + method->name);
      return false;
    }
    info.method_names.push_back(method->name);
    info.method_index[method->name] = idx++;
    info.methods[method->name] = method.get();
  }
  interfaces_[decl.name] = std::move(info);
  return true;
}

bool Codegen::emitClassStruct(const ast::ClassDeclStmt& decl) {
  if (classes_.count(decl.name) != 0) {
    error("redefinition of class: " + decl.name);
    return false;
  }

  ClassInfo info;
  info.type = llvm::StructType::create(globalContext(), decl.name);
  info.interfaces = decl.interfaces;
  info.ast = &decl;

  std::vector<llvm::Type*> elements;
  unsigned idx = 0;
  for (const auto& field : decl.fields) {
    if (!field) {
      continue;
    }
    if (info.field_index.count(field->name) != 0) {
      error("duplicate field '" + field->name + "' in class " + decl.name);
      return false;
    }
    if (isClassType(field->type_name) || isInterfaceType(field->type_name)) {
      error("nested class/interface fields not supported yet: " + field->type_name);
      return false;
    }
    llvm::Type* fty = llvmTypeForTypeName(field->type_name);
    info.field_index[field->name] = idx++;
    info.field_types.push_back(field->type_name);
    elements.push_back(fty);
  }
  info.type->setBody(elements, /*isPacked=*/false);
  classes_[decl.name] = std::move(info);
  return true;
}

bool Codegen::declareClassMethods(const ast::ClassDeclStmt& decl) {
  ClassInfo* info = findClassMut(decl.name);
  if (!info) {
    return false;
  }
  for (const auto& method : decl.methods) {
    if (!method) {
      continue;
    }
    if (info->methods.count(method->name) != 0) {
      error("duplicate method in class " + decl.name + ": " + method->name);
      return false;
    }
    std::vector<llvm::Type*> params;
    params.push_back(ptrTy());  // implicit this*
    for (const ast::Param& p : method->params) {
      params.push_back(typeFromName(p.type_name));
    }
    llvm::Type* ret_ty = typeFromName(method->return_type);
    auto* fty = llvm::FunctionType::get(ret_ty, params, false);
    const std::string mangled = mangleMethod(decl.name, method->name);
    llvm::Function* fn =
        llvm::Function::Create(fty, llvm::Function::InternalLinkage, mangled, module_.get());
    fn->arg_begin()->setName("this");
    info->methods[method->name] = fn;
  }
  return true;
}

bool Codegen::buildVtables(const ast::ClassDeclStmt& decl) {
  ClassInfo* info = findClassMut(decl.name);
  if (!info) {
    return false;
  }
  for (const std::string& iface_name : decl.interfaces) {
    const InterfaceInfo* iface = findInterface(iface_name);
    if (!iface) {
      error("class " + decl.name + " implements unknown interface " + iface_name);
      return false;
    }
    std::vector<llvm::Constant*> slots;
    for (const std::string& method_name : iface->method_names) {
      auto mit = info->methods.find(method_name);
      if (mit == info->methods.end()) {
        error("class " + decl.name + " missing method '" + method_name +
              "' for interface " + iface_name);
        return false;
      }
      slots.push_back(mit->second);
    }
    auto* arr_ty = llvm::ArrayType::get(ptrTy(), slots.size());
    llvm::Constant* init = llvm::ConstantArray::get(arr_ty, slots);
    auto* gv = new llvm::GlobalVariable(
        *module_, arr_ty, /*isConstant=*/true, llvm::GlobalValue::InternalLinkage, init,
        vtableName(decl.name, iface_name));
    info->vtables[iface_name] = gv;
  }
  return true;
}

bool Codegen::emitMethodBody(const std::string& class_name, const ast::FuncDeclStmt& method) {
  ClassInfo* info = findClassMut(class_name);
  if (!info) {
    return false;
  }
  llvm::Function* fn = info->methods[method.name];
  if (!fn || !fn->empty()) {
    error("bad method function for " + class_name + "." + method.name);
    return false;
  }

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  named_values_.clear();
  local_types_.clear();
  heap_locals_.clear();
  defer_scopes_.clear();
  enterDeferScope();

  current_class_ = class_name;
  current_this_ = &*fn->arg_begin();

  unsigned idx = 1;
  for (const ast::Param& param : method.params) {
    llvm::Argument& arg = *std::next(fn->arg_begin(), static_cast<int>(idx));
    arg.setName(param.name);
    llvm::AllocaInst* alloca = createEntryAlloca(fn, typeFromName(param.type_name), param.name);
    builder_.CreateStore(&arg, alloca);
    named_values_[param.name] = alloca;
    local_types_[param.name] = param.type_name;
    ++idx;
  }

  if (!emitStmtList(method.body)) {
    current_class_.clear();
    current_this_ = nullptr;
    return false;
  }

  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    if (!flushAllDefers()) {
      current_class_.clear();
      current_this_ = nullptr;
      return false;
    }
    if (fn->getReturnType()->isVoidTy()) {
      builder_.CreateRetVoid();
    } else if (fn->getReturnType()->isIntegerTy(32)) {
      builder_.CreateRet(llvm::ConstantInt::get(i32Ty(), 0));
    } else {
      error("missing return in method " + class_name + "." + method.name);
      current_class_.clear();
      current_this_ = nullptr;
      return false;
    }
  }
  defer_scopes_.clear();

  current_class_.clear();
  current_this_ = nullptr;

  if (llvm::verifyFunction(*fn, &llvm::errs())) {
    error("method verification failed: " + class_name + "." + method.name);
    fn->eraseFromParent();
    return false;
  }
  return true;
}

bool Codegen::emitClassMethodBodies(const ast::ClassDeclStmt& decl) {
  for (const auto& method : decl.methods) {
    if (!method) {
      continue;
    }
    if (!emitMethodBody(decl.name, *method)) {
      return false;
    }
  }
  return true;
}

bool Codegen::emit(const ast::Module& module) {
  had_error_ = false;
  named_values_.clear();
  local_types_.clear();
  classes_.clear();
  interfaces_.clear();
  generic_templates_.clear();
  specialized_funcs_.clear();
  type_subst_.clear();
  heap_locals_.clear();
  defer_scopes_.clear();
  fat_ptr_ty_ = nullptr;
  current_class_.clear();
  current_this_ = nullptr;

  ensureDataLayout();

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::InterfaceDecl) {
      continue;
    }
    if (!registerInterface(static_cast<const ast::InterfaceDeclStmt&>(*stmt))) {
      return false;
    }
  }

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    if (!emitClassStruct(static_cast<const ast::ClassDeclStmt&>(*stmt))) {
      return false;
    }
  }

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    const auto& cls = static_cast<const ast::ClassDeclStmt&>(*stmt);
    if (!declareClassMethods(cls) || !buildVtables(cls)) {
      return false;
    }
  }

  // Generic functions are templates — store, do not emit IR yet.
  // Concrete functions get declarations now.
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::FuncDecl) {
      continue;
    }
    const auto& fn = static_cast<const ast::FuncDeclStmt&>(*stmt);
    if (fn.is_method) {
      continue;
    }
    if (!fn.generics.empty()) {
      if (generic_templates_.count(fn.name) != 0) {
        error("redefinition of generic function: " + fn.name);
        return false;
      }
      generic_templates_[fn.name] = &fn;
      continue;
    }
    std::vector<llvm::Type*> params;
    for (const ast::Param& p : fn.params) {
      params.push_back(typeFromName(p.type_name));
    }
    llvm::Type* ret_ty = typeFromName(fn.return_type);
    auto* fty = llvm::FunctionType::get(ret_ty, params, false);
    llvm::Function::Create(fty, llvm::Function::ExternalLinkage, fn.name, module_.get());
  }

  getOrDeclarePrintInt();
  getOrDeclarePrintStr();
  getOrDeclareAlloc();
  getOrDeclareFree();
  fatPtrTy();

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    if (!emitClassMethodBodies(static_cast<const ast::ClassDeclStmt&>(*stmt))) {
      return false;
    }
  }

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    if (stmt->kind() == ast::StmtKind::InterfaceDecl ||
        stmt->kind() == ast::StmtKind::ClassDecl) {
      continue;
    }
    if (stmt->kind() == ast::StmtKind::FuncDecl) {
      const auto& fn = static_cast<const ast::FuncDeclStmt&>(*stmt);
      if (!fn.generics.empty()) {
        continue;  // templates stay unevaluated until call sites
      }
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
    case ast::StmtKind::InterfaceDecl:
    case ast::StmtKind::ClassDecl:
      return true;
    case ast::StmtKind::FieldDecl:
      error("field declaration outside class");
      return false;
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
    case ast::StmtKind::Defer:
      return emitDefer(static_cast<const ast::DeferStmt&>(stmt));
    case ast::StmtKind::Sync:
      return emitSync(static_cast<const ast::SyncBlockStmt&>(stmt));
  }
  error("unsupported statement in codegen");
  return false;
}

bool Codegen::emitSpecializedFunc(const ast::FuncDeclStmt& tmpl, const std::string& mangled,
                                  const std::unordered_map<std::string, std::string>& subst) {
  type_subst_ = subst;

  std::vector<llvm::Type*> params;
  std::vector<std::string> resolved_param_types;
  for (const ast::Param& p : tmpl.params) {
    const std::string resolved = resolveTypeName(p.type_name);
    resolved_param_types.push_back(resolved);
    // Class values are passed as pointers (same as method this).
    if (isClassType(resolved)) {
      params.push_back(ptrTy());
    } else {
      params.push_back(typeFromName(resolved));
    }
  }
  const std::string resolved_ret = resolveTypeName(tmpl.return_type);
  llvm::Type* ret_ty = typeFromName(resolved_ret);
  auto* fty = llvm::FunctionType::get(ret_ty, params, false);
  llvm::Function* fn =
      llvm::Function::Create(fty, llvm::Function::InternalLinkage, mangled, module_.get());
  specialized_funcs_[mangled] = fn;

  // Save caller locals; specialization is nested from a call site.
  auto saved_named = named_values_;
  auto saved_locals = local_types_;
  auto saved_heap = heap_locals_;
  auto saved_defers = defer_scopes_;
  auto saved_insert = builder_.GetInsertBlock();
  auto saved_point = builder_.GetInsertPoint();

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  named_values_.clear();
  local_types_.clear();
  heap_locals_.clear();
  defer_scopes_.clear();
  enterDeferScope();

  unsigned idx = 0;
  for (auto& arg : fn->args()) {
    const ast::Param& param = tmpl.params[idx];
    const std::string resolved = resolved_param_types[idx];
    arg.setName(param.name);
    if (isClassType(resolved)) {
      // Argument is a pointer to the caller's instance; copy into a local alloca so
      // field/method lowering can keep using AllocaInst* in named_values_.
      const ClassInfo* info = findClass(resolved);
      llvm::AllocaInst* slot = createEntryAlloca(fn, info->type, param.name);
      llvm::Value* copy = builder_.CreateLoad(info->type, &arg, param.name + ".copy");
      builder_.CreateStore(copy, slot);
      named_values_[param.name] = slot;
      local_types_[param.name] = resolved;
    } else {
      llvm::Type* pty = typeFromName(resolved);
      llvm::AllocaInst* alloca = createEntryAlloca(fn, pty, param.name);
      builder_.CreateStore(&arg, alloca);
      named_values_[param.name] = alloca;
      local_types_[param.name] = resolved;
    }
    ++idx;
  }

  bool ok = emitStmtList(tmpl.body);

  if (ok && builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    ok = flushAllDefers();
    if (ok) {
      if (fn->getReturnType()->isVoidTy()) {
        builder_.CreateRetVoid();
      } else if (fn->getReturnType()->isIntegerTy(32)) {
        builder_.CreateRet(llvm::ConstantInt::get(i32Ty(), 0));
      } else {
        error("missing return in specialized function: " + mangled);
        ok = false;
      }
    }
  }

  if (ok && llvm::verifyFunction(*fn, &llvm::errs())) {
    error("specialized function verification failed: " + mangled);
    ok = false;
  }

  type_subst_.clear();
  named_values_ = std::move(saved_named);
  local_types_ = std::move(saved_locals);
  heap_locals_ = std::move(saved_heap);
  defer_scopes_ = std::move(saved_defers);
  if (saved_insert) {
    builder_.SetInsertPoint(saved_insert, saved_point);
  }
  return ok;
}

llvm::Function* Codegen::ensureSpecialized(const ast::FuncDeclStmt& tmpl,
                                           const std::vector<std::string>& arg_type_names) {
  if (tmpl.params.size() != arg_type_names.size()) {
    error("argument count mismatch for generic call to " + tmpl.name);
    return nullptr;
  }

  std::unordered_map<std::string, std::string> subst;
  for (const ast::GenericParam& gp : tmpl.generics) {
    bool found = false;
    for (size_t i = 0; i < tmpl.params.size(); ++i) {
      if (tmpl.params[i].type_name == gp.name) {
        subst[gp.name] = arg_type_names[i];
        found = true;
        break;
      }
    }
    if (!found) {
      error("cannot infer generic parameter " + gp.name + " for " + tmpl.name);
      return nullptr;
    }
    const std::string& concrete = subst[gp.name];
    if (!isClassType(concrete)) {
      error("generic " + gp.name + " inferred non-class type " + concrete);
      return nullptr;
    }
    if (!classImplements(concrete, gp.constraint)) {
      error("type " + concrete + " does not implement " + gp.constraint);
      return nullptr;
    }
  }

  std::string mangled = tmpl.name;
  for (const ast::GenericParam& gp : tmpl.generics) {
    mangled += "_" + subst[gp.name];
  }

  auto it = specialized_funcs_.find(mangled);
  if (it != specialized_funcs_.end()) {
    return it->second;
  }
  if (!emitSpecializedFunc(tmpl, mangled, subst)) {
    return nullptr;
  }
  return specialized_funcs_[mangled];
}

bool Codegen::emitFuncDecl(const ast::FuncDeclStmt& fn_ast) {
  if (fn_ast.is_method) {
    return true;
  }
  if (!fn_ast.generics.empty()) {
    return true;  // template — specialized on demand
  }
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
  local_types_.clear();
  heap_locals_.clear();
  defer_scopes_.clear();
  enterDeferScope();

  unsigned idx = 0;
  for (auto& arg : fn->args()) {
    const ast::Param& param = fn_ast.params[idx];
    arg.setName(param.name);
    llvm::Type* pty = typeFromName(param.type_name);
    llvm::AllocaInst* alloca = createEntryAlloca(fn, pty, param.name);
    builder_.CreateStore(&arg, alloca);
    named_values_[param.name] = alloca;
    local_types_[param.name] = param.type_name;
    ++idx;
  }

  if (!emitStmtList(fn_ast.body)) {
    return false;
  }

  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    if (!flushAllDefers()) {
      return false;
    }
    if (fn->getReturnType()->isVoidTy()) {
      builder_.CreateRetVoid();
    } else if (fn->getReturnType()->isIntegerTy(32)) {
      builder_.CreateRet(llvm::ConstantInt::get(i32Ty(), 0));
    } else {
      error("missing return in non-i32 function: " + fn_ast.name);
      return false;
    }
  }
  defer_scopes_.clear();

  if (llvm::verifyFunction(*fn, &llvm::errs())) {
    error("function verification failed: " + fn_ast.name);
    fn->eraseFromParent();
    return false;
  }
  return true;
}

bool Codegen::initClassAtPtr(llvm::Value* base, const ClassInfo& info,
                             const ast::NewExpr& neu) {
  if (neu.args.size() > info.field_types.size()) {
    error("too many constructor arguments for " + neu.class_name);
    return false;
  }
  for (size_t i = 0; i < neu.args.size(); ++i) {
    if (!neu.args[i]) {
      continue;
    }
    llvm::Value* v = emitExpr(*neu.args[i]);
    if (!v) {
      return false;
    }
    llvm::Type* fty = llvmTypeForTypeName(info.field_types[i]);
    if (fty->isIntegerTy(32) && !v->getType()->isPointerTy()) {
      v = toI32(v);
    }
    llvm::Value* gep =
        builder_.CreateStructGEP(info.type, base, static_cast<unsigned>(i), "ctor.field");
    builder_.CreateStore(v, gep);
  }
  return true;
}

bool Codegen::initClassFromNew(llvm::AllocaInst* alloca, const ClassInfo& info,
                               const ast::NewExpr& neu) {
  return initClassAtPtr(alloca, info, neu);
}

bool Codegen::emitDefer(const ast::DeferStmt& stmt) {
  if (defer_scopes_.empty()) {
    enterDeferScope();
  }
  defer_scopes_.back().push_back(&stmt);
  return true;
}

bool Codegen::emitSync(const ast::SyncBlockStmt& stmt) {
  // Placeholder for a real mutex: sequential-consistency fences around the critical section.
  // Fence yields void — do not assign a value name.
  builder_.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent, llvm::SyncScope::System);
  if (!emitStmtListScoped(stmt.body)) {
    return false;
  }
  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent, llvm::SyncScope::System);
  }
  return true;
}

llvm::Value* Codegen::boxToInterface(llvm::Value* class_ptr, const std::string& class_name,
                                     const std::string& iface_name) {
  const ClassInfo* info = findClass(class_name);
  if (!info) {
    error("box: unknown class " + class_name);
    return nullptr;
  }
  auto vit = info->vtables.find(iface_name);
  if (vit == info->vtables.end()) {
    error("class " + class_name + " has no vtable for " + iface_name);
    return nullptr;
  }

  llvm::Value* data = builder_.CreateBitCast(class_ptr, ptrTy(), "iface.data");
  llvm::Value* vtable = builder_.CreateBitCast(vit->second, ptrTy(), "iface.vtable");

  llvm::Value* fat = llvm::UndefValue::get(fatPtrTy());
  fat = builder_.CreateInsertValue(fat, data, 0, "fat.data");
  fat = builder_.CreateInsertValue(fat, vtable, 1, "fat.vtable");
  return fat;
}

bool Codegen::emitVarDecl(const ast::VarDeclStmt& decl) {
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();

  if (isClassType(decl.type_name)) {
    const ClassInfo* info = findClass(decl.type_name);
    local_types_[decl.name] = decl.type_name;

    if (decl.init && decl.init->kind() == ast::ExprKind::Heap) {
      const auto& heap = static_cast<const ast::HeapExpr&>(*decl.init);
      if (!heap.inner || heap.inner->kind() != ast::ExprKind::New) {
        error("heap init requires ClassName(...)");
        return false;
      }
      const auto& neu = static_cast<const ast::NewExpr&>(*heap.inner);
      if (neu.class_name != decl.type_name) {
        error("class type mismatch in heap initialization of " + decl.name);
        return false;
      }
      llvm::AllocaInst* slot = createEntryAlloca(fn, ptrTy(), decl.name);
      llvm::Value* heap_ptr = emitExpr(*decl.init);
      if (!heap_ptr) {
        return false;
      }
      builder_.CreateStore(heap_ptr, slot);
      named_values_[decl.name] = slot;
      heap_locals_.insert(decl.name);
      return true;
    }

    llvm::AllocaInst* alloca = createEntryAlloca(fn, info->type, decl.name);
    named_values_[decl.name] = alloca;
    heap_locals_.erase(decl.name);
    if (decl.init) {
      if (decl.init->kind() != ast::ExprKind::New) {
        error("class variable must be initialized with ClassName(...) or heap ClassName(...)");
        return false;
      }
      const auto& neu = static_cast<const ast::NewExpr&>(*decl.init);
      if (neu.class_name != decl.type_name) {
        error("class type mismatch in initialization of " + decl.name);
        return false;
      }
      if (!initClassFromNew(alloca, *info, neu)) {
        return false;
      }
    }
    return true;
  }

  if (isInterfaceType(decl.type_name)) {
    llvm::AllocaInst* alloca = createEntryAlloca(fn, fatPtrTy(), decl.name);
    named_values_[decl.name] = alloca;
    local_types_[decl.name] = decl.type_name;

    if (!decl.init) {
      error("interface variable requires initializer");
      return false;
    }

    // Auto-box: IReader iface = p;
    if (decl.init->kind() == ast::ExprKind::Identifier) {
      const auto& id = static_cast<const ast::IdentifierExpr&>(*decl.init);
      auto lit = local_types_.find(id.name);
      if (lit == local_types_.end() || !isClassType(lit->second)) {
        error("interface init expects a class instance");
        return false;
      }
      llvm::Value* obj = classObjectPtr(id.name);
      if (!obj) {
        return false;
      }
      llvm::Value* fat = boxToInterface(obj, lit->second, decl.type_name);
      if (!fat) {
        return false;
      }
      builder_.CreateStore(fat, alloca);
      return true;
    }

    error("unsupported interface initializer");
    return false;
  }

  llvm::AllocaInst* alloca = createEntryAlloca(fn, i32Ty(), decl.name);
  llvm::Value* init = decl.init ? toI32(emitExpr(*decl.init))
                                : llvm::ConstantInt::get(i32Ty(), 0);
  if (!init) {
    return false;
  }
  builder_.CreateStore(init, alloca);
  named_values_[decl.name] = alloca;
  local_types_[decl.name] = decl.type_name.empty() ? "i32" : decl.type_name;
  return true;
}

llvm::Value* Codegen::emitLValue(const ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);

      // Method body: bare field name resolves against this.
      if (!current_class_.empty() && current_this_) {
        const ClassInfo* info = findClass(current_class_);
        if (info) {
          auto fit = info->field_index.find(id.name);
          if (fit != info->field_index.end()) {
            return builder_.CreateStructGEP(info->type, current_this_, fit->second, id.name);
          }
        }
      }

      auto it = named_values_.find(id.name);
      if (it == named_values_.end()) {
        error("unknown variable: " + id.name);
        return nullptr;
      }
      return it->second;
    }
    case ast::ExprKind::MemberAccess: {
      const auto& access = static_cast<const ast::MemberAccessExpr&>(expr);
      llvm::Value* base = nullptr;
      llvm::StructType* sty = nullptr;

      if (access.object->kind() == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*access.object);
        auto lit = local_types_.find(id.name);
        if (lit != local_types_.end() && isClassType(lit->second)) {
          const ClassInfo* info = findClass(lit->second);
          if (!info) {
            error("bad class object in member access");
            return nullptr;
          }
          base = classObjectPtr(id.name);
          if (!base) {
            return nullptr;
          }
          sty = info->type;
          auto fit = info->field_index.find(access.member);
          if (fit == info->field_index.end()) {
            error("unknown field: " + access.member);
            return nullptr;
          }
          return builder_.CreateStructGEP(sty, base, fit->second, access.member);
        }
      }

      error("member access on non-class value");
      return nullptr;
    }
    default:
      error("expression is not an assignable lvalue");
      return nullptr;
  }
}

bool Codegen::emitAssign(const ast::AssignStmt& assign) {
  if (!assign.target || !assign.value) {
    error("incomplete assignment");
    return false;
  }

  // Interface reassignment from class: iface = p;
  if (assign.target->kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(*assign.target);
    auto lit = local_types_.find(id.name);
    if (lit != local_types_.end() && isInterfaceType(lit->second)) {
      auto vit = named_values_.find(id.name);
      if (vit == named_values_.end()) {
        error("unknown interface variable: " + id.name);
        return false;
      }
      if (assign.value->kind() != ast::ExprKind::Identifier) {
        error("interface assign expects class instance");
        return false;
      }
      const auto& src = static_cast<const ast::IdentifierExpr&>(*assign.value);
      auto slit = local_types_.find(src.name);
      if (slit == local_types_.end() || !isClassType(slit->second)) {
        error("interface assign source must be a class local");
        return false;
      }
      llvm::Value* obj = classObjectPtr(src.name);
      if (!obj) {
        return false;
      }
      llvm::Value* fat = boxToInterface(obj, slit->second, lit->second);
      if (!fat) {
        return false;
      }
      builder_.CreateStore(fat, vit->second);
      return true;
    }
  }

  llvm::Value* ptr = emitLValue(*assign.target);
  if (!ptr) {
    return false;
  }
  llvm::Value* v = emitExpr(*assign.value);
  if (!v) {
    return false;
  }
  if (!v->getType()->isPointerTy()) {
    v = toI32(v);
  }
  builder_.CreateStore(v, ptr);
  return true;
}

bool Codegen::emitReturn(const ast::ReturnStmt& ret) {
  llvm::Value* v = nullptr;
  if (ret.value) {
    v = toI32(emitExpr(*ret.value));
    if (!v) {
      return false;
    }
  }
  // Evaluate return value first, then run deferred cleanups (LIFO), then ret.
  if (!flushAllDefers()) {
    return false;
  }
  if (!ret.value) {
    builder_.CreateRetVoid();
    return true;
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

  builder_.SetInsertPoint(then_bb);
  if (!emitStmtListScoped(stmt.then_body)) {
    return false;
  }
  branchTo(merge_bb);

  builder_.SetInsertPoint(else_bb);
  if (!emitStmtListScoped(stmt.else_body)) {
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
  if (!emitStmtListScoped(stmt.body)) {
    return false;
  }
  branchTo(cond_bb);

  builder_.SetInsertPoint(end_bb);
  return true;
}

llvm::Value* Codegen::emitMethodCall(const ast::MethodCallExpr& call) {
  if (!call.object) {
    error("method call missing object");
    return nullptr;
  }

  // Resolve object type from Identifier local.
  if (call.object->kind() != ast::ExprKind::Identifier) {
    error("method call object must be a local for now");
    return nullptr;
  }
  const auto& id = static_cast<const ast::IdentifierExpr&>(*call.object);
  auto lit = local_types_.find(id.name);
  auto vit = named_values_.find(id.name);
  if (lit == local_types_.end() || vit == named_values_.end()) {
    error("unknown object in method call: " + id.name);
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

  // Concrete class: direct call Class_method(this, ...)
  if (isClassType(lit->second)) {
    const ClassInfo* info = findClass(lit->second);
    auto mit = info->methods.find(call.method);
    if (mit == info->methods.end()) {
      error("unknown method " + lit->second + "." + call.method);
      return nullptr;
    }
    llvm::Value* self = classObjectPtr(id.name);
    if (!self) {
      return nullptr;
    }
    std::vector<llvm::Value*> call_args;
    call_args.push_back(self);
    call_args.insert(call_args.end(), args.begin(), args.end());
    if (mit->second->getReturnType()->isVoidTy()) {
      builder_.CreateCall(mit->second, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(mit->second, call_args, "mcall");
  }

  // Interface: fat-pointer vtable dispatch.
  if (isInterfaceType(lit->second)) {
    const InterfaceInfo* iface = findInterface(lit->second);
    auto mit = iface->method_index.find(call.method);
    if (mit == iface->method_index.end()) {
      error("unknown interface method " + lit->second + "." + call.method);
      return nullptr;
    }
    const unsigned slot = mit->second;
    const ast::FuncDeclStmt* sig = iface->methods.at(call.method);

    llvm::Value* fat = builder_.CreateLoad(fatPtrTy(), vit->second, id.name + ".fat");
    llvm::Value* data = builder_.CreateExtractValue(fat, 0, "iface.data");
    llvm::Value* vtable = builder_.CreateExtractValue(fat, 1, "iface.vtable");

    // vtable is ptr to [N x ptr]; GEP to slot then load fn ptr.
    auto* arr_ty = llvm::ArrayType::get(ptrTy(), iface->method_names.size());
    llvm::Value* slot_ptr = builder_.CreateInBoundsGEP(
        arr_ty, vtable,
        {llvm::ConstantInt::get(i32Ty(), 0), llvm::ConstantInt::get(i32Ty(), slot)},
        "vtable.slot");
    llvm::Value* fn_ptr = builder_.CreateLoad(ptrTy(), slot_ptr, "fnptr");

    std::vector<llvm::Type*> param_tys;
    param_tys.push_back(ptrTy());
    for (const ast::Param& p : sig->params) {
      param_tys.push_back(typeFromName(p.type_name));
    }
    llvm::Type* ret_ty = typeFromName(sig->return_type);
    auto* fty = llvm::FunctionType::get(ret_ty, param_tys, false);

    std::vector<llvm::Value*> call_args;
    call_args.push_back(data);
    call_args.insert(call_args.end(), args.begin(), args.end());

    if (ret_ty->isVoidTy()) {
      builder_.CreateCall(fty, fn_ptr, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(fty, fn_ptr, call_args, "iface.call");
  }

  error("method call on non-object type");
  return nullptr;
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

      // Method body: field load via this.
      if (!current_class_.empty() && current_this_) {
        const ClassInfo* info = findClass(current_class_);
        if (info) {
          auto fit = info->field_index.find(id.name);
          if (fit != info->field_index.end()) {
            llvm::Value* gep =
                builder_.CreateStructGEP(info->type, current_this_, fit->second, id.name);
            llvm::Type* fty = info->type->getElementType(fit->second);
            return builder_.CreateLoad(fty, gep, id.name);
          }
        }
      }

      auto it = named_values_.find(id.name);
      if (it == named_values_.end()) {
        error("unknown variable: " + id.name);
        return nullptr;
      }
      llvm::AllocaInst* alloca = it->second;
      llvm::Type* ty = alloca->getAllocatedType();
      auto lit = local_types_.find(id.name);
      if (lit != local_types_.end() && isClassType(lit->second)) {
        return classObjectPtr(id.name);
      }
      if (ty->isStructTy()) {
        if (lit != local_types_.end() && isInterfaceType(lit->second)) {
          return builder_.CreateLoad(fatPtrTy(), alloca, id.name);
        }
        if (ty == fatPtrTy()) {
          return builder_.CreateLoad(fatPtrTy(), alloca, id.name);
        }
        return alloca;
      }
      return builder_.CreateLoad(ty, alloca, id.name);
    }
    case ast::ExprKind::Heap:
      return emitHeapExpr(static_cast<const ast::HeapExpr&>(expr));
    case ast::ExprKind::StringLiteral: {
      const auto& lit = static_cast<const ast::StringLiteralExpr&>(expr);
      // Global null-terminated C string; returns i8* / opaque ptr.
      return builder_.CreateGlobalString(lit.value, "str");
    }
    case ast::ExprKind::MemberAccess: {
      const auto& access = static_cast<const ast::MemberAccessExpr&>(expr);
      llvm::Value* ptr = emitLValue(access);
      if (!ptr) {
        return nullptr;
      }
      llvm::Type* load_ty = i32Ty();
      if (access.object->kind() == ast::ExprKind::Identifier) {
        const auto& oid = static_cast<const ast::IdentifierExpr&>(*access.object);
        auto lit = local_types_.find(oid.name);
        if (lit != local_types_.end() && isClassType(lit->second)) {
          const ClassInfo* info = findClass(lit->second);
          auto fit = info->field_index.find(access.member);
          if (fit != info->field_index.end()) {
            load_ty = llvmTypeForTypeName(info->field_types[fit->second]);
          }
        }
      }
      return builder_.CreateLoad(load_ty, ptr, access.member);
    }
    case ast::ExprKind::MethodCall:
      return emitMethodCall(static_cast<const ast::MethodCallExpr&>(expr));
    case ast::ExprKind::New: {
      const auto& neu = static_cast<const ast::NewExpr&>(expr);
      const ClassInfo* info = findClass(neu.class_name);
      if (!info) {
        error("unknown class in NewExpr: " + neu.class_name);
        return nullptr;
      }
      llvm::Function* fn = builder_.GetInsertBlock()->getParent();
      llvm::AllocaInst* tmp = createEntryAlloca(fn, info->type, neu.class_name + ".tmp");
      if (!initClassFromNew(tmp, *info, neu)) {
        return nullptr;
      }
      return tmp;
    }
    case ast::ExprKind::Binary: {
      const auto& bin = static_cast<const ast::BinaryExpr&>(expr);

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
    case ast::ExprKind::Call:
      return emitGenericOrPlainCall(static_cast<const ast::CallExpr&>(expr));
  }
  error("unsupported expression in codegen");
  return nullptr;
}

llvm::Value* Codegen::emitHeapExpr(const ast::HeapExpr& expr) {
  if (!expr.inner || expr.inner->kind() != ast::ExprKind::New) {
    error("heap requires ClassName(...) construction");
    return nullptr;
  }
  const auto& neu = static_cast<const ast::NewExpr&>(*expr.inner);
  const ClassInfo* info = findClass(neu.class_name);
  if (!info) {
    error("unknown class in heap expression: " + neu.class_name);
    return nullptr;
  }
  ensureDataLayout();
  const uint64_t bytes = module_->getDataLayout().getTypeAllocSize(info->type);
  llvm::Value* size = llvm::ConstantInt::get(i64Ty(), bytes);
  llvm::Value* raw = builder_.CreateCall(getOrDeclareAlloc(), {size}, "heap.raw");
  if (!initClassAtPtr(raw, *info, neu)) {
    return nullptr;
  }
  return raw;
}

llvm::Value* Codegen::emitGenericOrPlainCall(const ast::CallExpr& call) {
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
  if (call.callee == "print_str" || call.callee == "l_print_str") {
    if (call.args.size() != 1) {
      error("l_print_str expects exactly 1 argument");
      return nullptr;
    }
    llvm::Value* arg = emitExpr(*call.args[0]);
    if (!arg) {
      return nullptr;
    }
    if (!arg->getType()->isPointerTy()) {
      error("l_print_str expects a ptr argument");
      return nullptr;
    }
    builder_.CreateCall(getOrDeclarePrintStr(), {arg});
    return llvm::ConstantInt::get(i32Ty(), 0);
  }
  if (call.callee == "l_free" || call.callee == "free") {
    if (call.args.size() != 1) {
      error("l_free expects exactly 1 argument");
      return nullptr;
    }
    llvm::Value* arg = nullptr;
    if (call.args[0]->kind() == ast::ExprKind::Identifier) {
      const auto& id = static_cast<const ast::IdentifierExpr&>(*call.args[0]);
      auto lit = local_types_.find(id.name);
      if (lit != local_types_.end() && isClassType(lit->second)) {
        arg = classObjectPtr(id.name);
      }
    }
    if (!arg) {
      arg = emitExpr(*call.args[0]);
    }
    if (!arg) {
      return nullptr;
    }
    if (!arg->getType()->isPointerTy()) {
      error("l_free expects a pointer");
      return nullptr;
    }
    builder_.CreateCall(getOrDeclareFree(), {arg});
    return llvm::ConstantInt::get(i32Ty(), 0);
  }

  // Generic monomorphization: print_val(p) -> print_val_Point
  auto git = generic_templates_.find(call.callee);
  if (git != generic_templates_.end()) {
    const ast::FuncDeclStmt& tmpl = *git->second;
    std::vector<std::string> arg_types;
    std::vector<llvm::Value*> call_args;
    for (size_t i = 0; i < call.args.size(); ++i) {
      if (!call.args[i]) {
        error("null argument in generic call");
        return nullptr;
      }
      // Infer type from identifier locals when possible.
      std::string ty_name;
      if (call.args[i]->kind() == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*call.args[i]);
        auto lit = local_types_.find(id.name);
        if (lit != local_types_.end()) {
          ty_name = lit->second;
        }
      }
      if (ty_name.empty()) {
        error("cannot infer type for argument " + std::to_string(i) + " of " + call.callee);
        return nullptr;
      }
      arg_types.push_back(ty_name);

      if (isClassType(ty_name)) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*call.args[i]);
        llvm::Value* obj = classObjectPtr(id.name);
        if (!obj) {
          return nullptr;
        }
        call_args.push_back(obj);
      } else {
        llvm::Value* v = emitExpr(*call.args[i]);
        if (!v) {
          return nullptr;
        }
        call_args.push_back(toI32(v));
      }
    }

    llvm::Function* specialized = ensureSpecialized(tmpl, arg_types);
    if (!specialized) {
      return nullptr;
    }
    if (specialized->getReturnType()->isVoidTy()) {
      builder_.CreateCall(specialized, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(specialized, call_args, "gcall");
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
  unsigned ai = 0;
  for (const ast::ExprPtr& arg : call.args) {
    llvm::Value* v = emitExpr(*arg);
    if (!v) {
      return nullptr;
    }
    llvm::Type* expected = callee->getFunctionType()->getParamType(ai++);
    if (expected->isIntegerTy(32) && !v->getType()->isPointerTy()) {
      v = toI32(v);
    }
    args.push_back(v);
  }
  if (callee->getReturnType()->isVoidTy()) {
    builder_.CreateCall(callee, args);
    return llvm::ConstantInt::get(i32Ty(), 0);
  }
  return builder_.CreateCall(callee, args, "calltmp");
}

}  // namespace l::codegen
