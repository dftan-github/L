#include "codegen/codegen.hpp"

#include <cstdint>
#include <iostream>
#include <iterator>
#include <utility>

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"

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

// Logical field `age` → physical LLVM / field_index key `age_` ([link:69-Banned-Trailing-Underscores-Field-Syntax]).
std::string physicalFieldName(const std::string& logical) {
  return logical + "_";
}

std::string displayFieldName(const std::string& physical) {
  if (!physical.empty() && physical.back() == '_') {
    return physical.substr(0, physical.size() - 1);
  }
  return physical;
}

bool isModuleTopLevelExecStmt(const ast::Stmt& stmt) {
  // Pure config files (e.g. app_cfg.l) may only contain relative-init blocks.
  switch (stmt.kind()) {
    case ast::StmtKind::Block:
    case ast::StmtKind::Assign:
    case ast::StmtKind::ExprStmt:
      return true;
    default:
      return false;
  }
}

}  // namespace

llvm::LLVMContext& globalContext() {
  static llvm::LLVMContext context;
  return context;
}

Codegen::Codegen(std::string module_name, diag::DiagnosticEngine* diags)
    : module_(std::make_unique<llvm::Module>(std::move(module_name), globalContext())),
      builder_(globalContext()),
      diags_(diags) {}

// --- Debug info ------------------------------------------------------------
// Header-only plumbing: DICompileUnit + DISubprogram per function, and a
// DILocation per statement. `initDebugInfo` runs after emit() when `-g` is set,
// because codegen only knows the real source file names once it walks the AST.

void Codegen::initDebugInfo(const std::string& module_name) {
  const std::string file = module_name.empty() ? std::string("<module>") : module_name;
  di_builder_ = std::make_unique<llvm::DIBuilder>(*module_);
  di_file_ = di_builder_->createFile(file, file);
  // Producer/optimization flags are informational; `-g` always implies DWARF.
  di_cu_ = di_builder_->createCompileUnit(llvm::dwarf::DW_LANG_C99, di_file_, "L", opt_level_ != 0,
                                          "", 0, "", llvm::DICompileUnit::FullDebug);
  di_files_.clear();
  di_files_[file] = di_file_;
  di_sp_ = nullptr;
  di_scope_ = nullptr;
}

void Codegen::finishDebugInfo() {
  if (!di_builder_) {
    return;
  }
  di_builder_->finalize();
  di_builder_.reset();
}

llvm::DIFile* Codegen::debugFile(const std::string& filename) {
  const std::string key =
      filename.empty() ? (di_file_->getFilename().str()) : filename;
  auto it = di_files_.find(key);
  if (it != di_files_.end()) {
    return it->second;
  }
  llvm::DIFile* f = di_builder_->createFile(key, key);
  di_files_[key] = f;
  return f;
}

llvm::DISubprogram* Codegen::debugSubprogram(llvm::Function* fn, const ast::FuncDeclStmt& decl,
                                             llvm::DIFile* file) {
  if (!di_builder_ || !fn) {
    return nullptr;
  }
  const unsigned line = decl.loc.line ? decl.loc.line : 1;
  llvm::DISubroutineType* st =
      di_builder_->createSubroutineType(di_builder_->getOrCreateTypeArray({}));
  llvm::DISubprogram* sp = di_builder_->createFunction(
      file, decl.name, fn->getName(), file, line, st, line, llvm::DINode::FlagZero,
      llvm::DISubprogram::SPFlagDefinition | llvm::DISubprogram::SPFlagLocalToUnit);
  fn->setSubprogram(sp);
  return sp;
}

void Codegen::setDebugLoc(const ast::SourceLocation& loc) {
  if (!di_builder_ || !di_sp_ || loc.line == 0) {
    return;
  }
  llvm::DILocalScope* scope = di_scope_ ? static_cast<llvm::DILocalScope*>(di_scope_) : di_sp_;
  builder_.SetCurrentDebugLocation(llvm::DILocation::get(
      module_->getContext(), loc.line, loc.column ? loc.column : 1, scope));
}

void Codegen::clearDebugLoc() {
  if (di_builder_) {
    builder_.SetCurrentDebugLocation(llvm::DebugLoc());
  }
}

llvm::Type* Codegen::i32Ty() {
  return llvm::Type::getInt32Ty(globalContext());
}

llvm::Type* Codegen::i64Ty() {
  return llvm::Type::getInt64Ty(globalContext());
}

llvm::Type* Codegen::f64Ty() {
  return llvm::Type::getDoubleTy(globalContext());
}

llvm::Type* Codegen::i1Ty() {
  return llvm::Type::getInt1Ty(globalContext());
}

int Codegen::numericRank(const std::string& ty) {
  if (ty == "i32") {
    return 1;
  }
  if (ty == "i64") {
    return 2;
  }
  if (ty == "f64") {
    return 3;
  }
  return 0;
}

bool Codegen::isNumericType(const std::string& ty) {
  return numericRank(ty) > 0;
}

std::string Codegen::promoteNumericTypes(const std::string& a, const std::string& b) {
  const int ra = numericRank(a);
  const int rb = numericRank(b);
  if (ra == 0 || rb == 0) {
    return {};
  }
  const int r = ra > rb ? ra : rb;
  if (r == 3) {
    return "f64";
  }
  if (r == 2) {
    return "i64";
  }
  return "i32";
}

llvm::Value* Codegen::emitNumericCast(llvm::Value* v, const std::string& from_ty,
                                      const std::string& to_ty,
                                      const ast::SourceLocation& loc) {
  if (!v || from_ty == to_ty) {
    return v;
  }
  if (to_ty == "i64") {
    if (from_ty == "i32") {
      return builder_.CreateSExt(v, i64Ty(), "prom.i64");
    }
  }
  if (to_ty == "f64") {
    if (from_ty == "i32") {
      return builder_.CreateSIToFP(v, f64Ty(), "prom.f64");
    }
    if (from_ty == "i64") {
      return builder_.CreateSIToFP(v, f64Ty(), "prom.f64");
    }
  }
  error(loc, "cannot promote '" + from_ty + "' to '" + to_ty + "'");
  return nullptr;
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

llvm::StructType* Codegen::fatClosureTy() {
  // Escaped closure: { fn_ptr, ctx_ptr } — same dual-pointer layout as interface fat ptr.
  return fatPtrTy();
}

bool Codegen::isClassType(const std::string& name) const {
  return classes_.find(resolveTypeName(name)) != classes_.end();
}

bool Codegen::isInterfaceType(const std::string& name) const {
  return interfaces_.find(name) != interfaces_.end();
}

const Codegen::ClassInfo* Codegen::findClass(const std::string& name) const {
  auto it = classes_.find(resolveTypeName(name));
  if (it == classes_.end()) {
    return nullptr;
  }
  return &it->second;
}

Codegen::ClassInfo* Codegen::findClassMut(const std::string& name) {
  auto it = classes_.find(resolveTypeName(name));
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

bool Codegen::isFunTypeName(const std::string& name) {
  return name.size() >= 4 && name.compare(0, 4, "fun(") == 0;
}

bool Codegen::parseFunTypeName(const std::string& name, std::vector<std::string>& params,
                               std::string& ret, bool& has_explicit_ret) {
  params.clear();
  ret = "void";
  has_explicit_ret = false;
  if (!isFunTypeName(name)) {
    return false;
  }
  std::string core = name;
  const auto arrow = name.rfind("->");
  if (arrow != std::string::npos && arrow > 4) {
    // Ensure arrow is outside the top-level fun(...) param list.
    int depth = 0;
    bool arrow_ok = false;
    for (size_t i = 0; i < name.size(); ++i) {
      if (name[i] == '(') {
        ++depth;
      } else if (name[i] == ')') {
        --depth;
      } else if (depth == 0 && i == arrow) {
        arrow_ok = true;
        break;
      }
    }
    if (arrow_ok) {
      has_explicit_ret = true;
      ret = name.substr(arrow + 2);
      core = name.substr(0, arrow);
    }
  }
  if (core.size() < 5 || core.front() != 'f' || core.back() != ')') {
    return false;
  }
  const std::string inner = core.substr(4, core.size() - 5);  // between fun( and )
  if (inner.empty()) {
    return true;
  }
  int depth = 0;
  std::string cur;
  for (char c : inner) {
    if (c == '<' || c == '(') {
      ++depth;
      cur.push_back(c);
    } else if (c == '>' || c == ')') {
      --depth;
      cur.push_back(c);
    } else if (c == ',' && depth == 0) {
      params.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) {
    params.push_back(cur);
  }
  return true;
}

std::string Codegen::resolveTypeName(const std::string& name) const {
  auto it = type_subst_.find(name);
  if (it != type_subst_.end()) {
    return it->second;
  }
  // fun(T) / fun(T)->R under active subst.
  if (isFunTypeName(name)) {
    std::vector<std::string> params;
    std::string ret;
    bool has_ret = false;
    if (!parseFunTypeName(name, params, ret, has_ret)) {
      return name;
    }
    std::string out = "fun(";
    for (size_t i = 0; i < params.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      out += resolveTypeName(params[i]);
    }
    out.push_back(')');
    if (has_ret) {
      out += "->";
      out += resolveTypeName(ret);
    }
    return out;
  }
  // Nested generics: Optional<V> → Optional<i32> under active type_subst_.
  std::string base;
  std::vector<std::string> args;
  if (splitGenericType(name, base, args)) {
    std::string out = base;
    out.push_back('<');
    for (size_t i = 0; i < args.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      out += resolveTypeName(args[i]);
    }
    out.push_back('>');
    return out;
  }
  // `use pkg::Config as Cfg` — an aliased type name resolves to the class stem.
  auto alias_it = use_aliases_.find(name);
  if (alias_it != use_aliases_.end() && !alias_it->second.empty()) {
    const std::string& target = alias_it->second;
    const std::size_t sep = target.rfind("::");
    return sep == std::string::npos ? target : target.substr(sep + 2);
  }
  return name;
}

std::string expandAliasedScope(
    const std::unordered_map<std::string, std::string>& aliases, const std::string& chain) {
  const std::size_t head_len = chain.find("::");
  const std::string head = chain.substr(0, head_len);
  auto it = aliases.find(head);
  if (it == aliases.end() || it->second.empty()) {
    return chain;
  }
  return head_len == std::string::npos ? it->second : it->second + chain.substr(head_len);
}

std::string Codegen::mangleName(const std::string& module_name, const std::string& name,
                                bool is_pub) {
  if (is_pub || name == "main" || module_name.empty()) {
    return name;
  }
  return module_name + "_" + name;
}

std::string Codegen::resolveFuncIrName(const std::string& callee) const {
  auto pub = public_funcs_.find(callee);
  if (pub != public_funcs_.end()) {
    return pub->second;
  }
  if (!current_module_.empty()) {
    auto priv = private_funcs_.find(current_module_ + "::" + callee);
    if (priv != private_funcs_.end()) {
      return priv->second;
    }
  }
  if (module_->getFunction(callee)) {
    return callee;
  }
  return {};
}

llvm::FunctionType* Codegen::llvmFunctionTypeForFunName(const std::string& fun_type_name,
                                                        const ast::SourceLocation& loc) {
  std::vector<std::string> params;
  std::string ret;
  bool has_ret = false;
  if (!parseFunTypeName(resolveTypeName(fun_type_name), params, ret, has_ret)) {
    error(loc, "invalid function type: " + fun_type_name);
    return nullptr;
  }
  llvm::Type* rty = (ret == "void") ? voidTy() : llvmTypeForTypeName(ret);
  std::vector<llvm::Type*> pts;
  for (const std::string& p : params) {
    pts.push_back(abiTypeForParam(p));
  }
  return llvm::FunctionType::get(rty, pts, /*isVarArg=*/false);
}

llvm::Type* Codegen::llvmTypeForTypeName(const std::string& name) {
  const std::string resolved = resolveTypeName(name);
  if (resolved == "void") {
    return voidTy();
  }
  if (resolved == "ptr" || resolved == "Arena" || isFunTypeName(resolved)) {
    return ptrTy();
  }
  if (resolved == "bool") {
    return i1Ty();
  }
  if (resolved == "i32") {
    return i32Ty();
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

llvm::Type* Codegen::abiTypeForParam(const std::string& type_name) {
  const std::string resolved = resolveTypeName(type_name);
  if (isFunTypeName(resolved)) {
    return ptrTy();
  }
  ensureTypeAvailable(type_name);
  // Classes/structs: always pass by reference (pointer). Never deep-copy.
  if (isClassType(resolved)) {
    return ptrTy();
  }
  // Interfaces: pass the fat pointer struct by value.
  return typeFromName(resolved);
}

void Codegen::bindParam(llvm::Function* fn, llvm::Value* arg, const ast::Param& param,
                        const std::string& resolved_type) {
  if (isClassType(resolved_type)) {
    // Incoming ABI value is Class*; keep it as a pointer local (no struct copy).
    llvm::AllocaInst* slot = createEntryAlloca(fn, ptrTy(), param.name);
    builder_.CreateStore(arg, slot);
    named_values_[param.name] = slot;
    local_types_[param.name] = resolved_type;
    heap_locals_.insert(param.name);
    if (!param.is_mutable) {
      readonly_refs_.insert(param.name);
    }
    return;
  }
  if (isFunTypeName(resolved_type)) {
    llvm::AllocaInst* slot = createEntryAlloca(fn, ptrTy(), param.name);
    builder_.CreateStore(arg, slot);
    named_values_[param.name] = slot;
    local_types_[param.name] = resolved_type;
    return;
  }
  llvm::Type* pty = typeFromName(resolved_type);
  llvm::AllocaInst* alloca = createEntryAlloca(fn, pty, param.name);
  builder_.CreateStore(arg, alloca);
  named_values_[param.name] = alloca;
  local_types_[param.name] = resolved_type;
}

bool Codegen::rejectReadonlyMutation(const ast::Expr& target, const ast::SourceLocation& loc) {
  const ast::Expr* cur = &target;
  while (cur) {
    if (cur->kind() == ast::ExprKind::MemberAccess) {
      cur = static_cast<const ast::MemberAccessExpr*>(cur)->object.get();
      continue;
    }
    if (cur->kind() == ast::ExprKind::Index) {
      cur = static_cast<const ast::IndexExpr*>(cur)->object.get();
      continue;
    }
    break;
  }
  if (cur && cur->kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(*cur);
    if (readonly_refs_.count(id.name) != 0) {
      error(loc, "cannot mutate read-only parameter '" + id.name +
                     "' (add '!' after the parameter name to allow mutation)");
      return true;
    }
  }
  return false;
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
  llvm::AllocaInst* ai = tmp.CreateAlloca(ty, nullptr, name);
  return ai;
}

// Local storage for a variable: a frame slot inside an `async fun` body (so it
// survives the suspend point), a plain stack alloca everywhere else.
llvm::Value* Codegen::createLocalStorage(llvm::Function* fn, llvm::Type* ty,
                                        const std::string& name) {
  if (in_coroutine_ && coro_frame_ && coro_frame_ty_) {
    auto it = coro_slot_addrs_.find(name);
    if (it != coro_slot_addrs_.end()) {
      return it->second;
    }
  }
  return createEntryAlloca(fn, ty, name);
}

// Pointee type of a local slot: an `alloca` yields its stored type, a coroutine
// frame `GEP` yields the struct field type.
llvm::Type* Codegen::localSlotType(llvm::Value* slot) {
  if (!slot) {
    return nullptr;
  }
  if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(slot)) {
    return ai->getAllocatedType();
  }
  if (auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(slot)) {
    return gep->getResultElementType();
  }
  return nullptr;
}

llvm::Function* Codegen::requireExtern(const std::string& name, const ast::SourceLocation& loc) {
  if (llvm::Function* existing = module_->getFunction(name)) {
    return existing;
  }
  error(loc, "undeclared external function '" + name +
        "' (declare it with `extern module` in stdlib)");
  return nullptr;
}

// [link:109-Performance-First-Architecture] FNV-1a 64-bit over an in-memory C string, emitted as straight-line IR.
// Per byte: load i8 -> zext i64 -> xor acc -> mul prime. The loop is a
// rotate-free 2-PHI loop, so LLVM keeps it tight and inlines the whole thing at
// the call site (no function body is generated — these are instructions only).
llvm::Value* Codegen::emitInlineStringHash(llvm::Value* cstr, llvm::Value* len,
                                           const std::string& name_hint) {
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* entry_bb = builder_.GetInsertBlock();
  llvm::BasicBlock* loop_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".hash.loop", fn);
  llvm::BasicBlock* body_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".hash.body", fn);
  llvm::BasicBlock* done_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".hash.done", fn);

  // FNV-1a constants: offset basis 14695981039346656037, prime 1099511628211.
  const llvm::APInt kOffset(64, 0xcbf29ce484222325ULL);
  const llvm::APInt kPrime(64, 0x100000001b3ULL);
  llvm::Value* seed = llvm::ConstantInt::get(i64Ty(), kOffset);
  llvm::Value* prime = llvm::ConstantInt::get(i64Ty(), kPrime);

  builder_.CreateBr(loop_bb);

  builder_.SetInsertPoint(loop_bb);
  llvm::PHINode* idx = builder_.CreatePHI(i64Ty(), 2, name_hint + ".hash.idx");
  llvm::PHINode* acc = builder_.CreatePHI(i64Ty(), 2, name_hint + ".hash.acc");
  idx->addIncoming(llvm::ConstantInt::get(i64Ty(), 0), entry_bb);
  acc->addIncoming(seed, entry_bb);
  llvm::Value* in_range = builder_.CreateICmpSLT(idx, len, name_hint + ".hash.cont");
  builder_.CreateCondBr(in_range, body_bb, done_bb);

  builder_.SetInsertPoint(body_bb);
  llvm::Value* byte_ptr = builder_.CreateGEP(llvm::Type::getInt8Ty(globalContext()), cstr,
                                             {idx}, name_hint + ".hash.ptr");
  llvm::Value* byte = builder_.CreateLoad(llvm::Type::getInt8Ty(globalContext()), byte_ptr,
                                          name_hint + ".hash.byte");
  llvm::Value* byte64 = builder_.CreateZExt(byte, i64Ty(), name_hint + ".hash.zext");
  llvm::Value* mixed = builder_.CreateXor(acc, byte64, name_hint + ".hash.xor");
  llvm::Value* next_acc = builder_.CreateMul(mixed, prime, name_hint + ".hash.mul");
  llvm::Value* next_idx = builder_.CreateAdd(idx, llvm::ConstantInt::get(i64Ty(), 1),
                                             name_hint + ".hash.next");
  idx->addIncoming(next_idx, body_bb);
  acc->addIncoming(next_acc, body_bb);
  builder_.CreateBr(loop_bb);

  builder_.SetInsertPoint(done_bb);
  return acc;
}

// [link:109-Performance-First-Architecture] Inline C-string equality returning i32 (1 / 0), matching the runtime's
// NULL-safe contract: identical pointers are equal; any NULL operand is not.
llvm::Value* Codegen::emitInlineStringEq(llvm::Value* a, llvm::Value* b,
                                         const std::string& name_hint) {
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* entry_bb = builder_.GetInsertBlock();
  llvm::BasicBlock* same_ptr_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".eq.same", fn);
  llvm::BasicBlock* null_chk_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".eq.nullchk", fn);
  llvm::BasicBlock* loop_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".eq.loop", fn);
  llvm::BasicBlock* body_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".eq.body", fn);
  llvm::BasicBlock* next_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".eq.next", fn);
  llvm::BasicBlock* unequal_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".eq.unequal", fn);
  llvm::BasicBlock* equal_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".eq.equal", fn);
  llvm::BasicBlock* done_bb =
      llvm::BasicBlock::Create(globalContext(), name_hint + ".eq.done", fn);

  llvm::Type* i8t = llvm::Type::getInt8Ty(globalContext());
  llvm::Value* zero32 = llvm::ConstantInt::get(i32Ty(), 0);
  llvm::Value* one32 = llvm::ConstantInt::get(i32Ty(), 1);
  llvm::Value* null_ptr =
      llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy()));

  // a == b (pointer) -> equal.
  llvm::Value* same = builder_.CreateICmpEQ(a, b, name_hint + ".eq.sameptr");
  builder_.CreateCondBr(same, same_ptr_bb, null_chk_bb);

  builder_.SetInsertPoint(same_ptr_bb);
  builder_.CreateBr(done_bb);

  // a == NULL || b == NULL -> not equal.
  builder_.SetInsertPoint(null_chk_bb);
  llvm::Value* a_null = builder_.CreateICmpEQ(a, null_ptr, name_hint + ".eq.anull");
  llvm::Value* b_null = builder_.CreateICmpEQ(b, null_ptr, name_hint + ".eq.bnull");
  llvm::Value* any_null = builder_.CreateOr(a_null, b_null, name_hint + ".eq.anynull");
  builder_.CreateCondBr(any_null, unequal_bb, loop_bb);

  // Byte loop: compare one byte at a time; stop on a mismatch or a terminator.
  builder_.SetInsertPoint(loop_bb);
  llvm::PHINode* idx = builder_.CreatePHI(i64Ty(), 2, name_hint + ".eq.idx");
  idx->addIncoming(llvm::ConstantInt::get(i64Ty(), 0), null_chk_bb);
  llvm::Value* pa = builder_.CreateGEP(i8t, a, {idx}, name_hint + ".eq.pa");
  llvm::Value* pb = builder_.CreateGEP(i8t, b, {idx}, name_hint + ".eq.pb");
  llvm::Value* ca = builder_.CreateLoad(i8t, pa, name_hint + ".eq.ca");
  llvm::Value* cb = builder_.CreateLoad(i8t, pb, name_hint + ".eq.cb");
  llvm::Value* differ = builder_.CreateICmpNE(ca, cb, name_hint + ".eq.differ");
  builder_.CreateCondBr(differ, unequal_bb, body_bb);

  // Equal bytes: a NUL here means both strings ended -> equal.
  builder_.SetInsertPoint(body_bb);
  llvm::Value* is_term = builder_.CreateICmpEQ(ca, llvm::ConstantInt::get(i8t, 0),
                                               name_hint + ".eq.term");
  builder_.CreateCondBr(is_term, equal_bb, next_bb);

  // Otherwise advance and re-enter the loop.
  builder_.SetInsertPoint(next_bb);
  llvm::Value* next_idx =
      builder_.CreateAdd(idx, llvm::ConstantInt::get(i64Ty(), 1), name_hint + ".eq.inc");
  idx->addIncoming(next_idx, next_bb);
  builder_.CreateBr(loop_bb);

  builder_.SetInsertPoint(unequal_bb);
  builder_.CreateBr(done_bb);
  builder_.SetInsertPoint(equal_bb);
  builder_.CreateBr(done_bb);

  builder_.SetInsertPoint(done_bb);
  llvm::PHINode* result = builder_.CreatePHI(i32Ty(), 3, name_hint + ".eq.result");
  result->addIncoming(one32, same_ptr_bb);
  result->addIncoming(zero32, unequal_bb);
  result->addIncoming(one32, equal_bb);
  return result;
}

// [link:109-Performance-First-Architecture] Dispatch on the callee name: FNV-1a and str_eq become inline IR.
llvm::Value* Codegen::tryEmitInlinedStringBuiltin(const ast::CallExpr& call,
                                                  const std::vector<llvm::Value*>& args) {
  if (call.callee == "l_hash_string" && args.size() == 1 && args[0] &&
      args[0]->getType()->isPointerTy()) {
    // Length comes from libc strlen (always i64 / size_t), avoiding a dependency
    // on which stdlib declared l_str_len and its i32 return type.
    llvm::Function* strlen_fn = module_->getFunction("strlen");
    if (!strlen_fn) {
      auto* sfty = llvm::FunctionType::get(i64Ty(), {ptrTy()}, false);
      strlen_fn = llvm::Function::Create(sfty, llvm::Function::ExternalLinkage, "strlen",
                                         module_.get());
      strlen_fn->setCallingConv(llvm::CallingConv::C);
    }
    llvm::Value* len = builder_.CreateCall(strlen_fn, {args[0]}, "hash.len");
    return emitInlineStringHash(args[0], len, "l_hash_string");
  }
  if (call.callee == "l_str_eq" && args.size() == 2 && args[0] && args[1] &&
      args[0]->getType()->isPointerTy() && args[1]->getType()->isPointerTy()) {
    return emitInlineStringEq(args[0], args[1], "l_str_eq");
  }
  return nullptr;
}

bool Codegen::declareExternFunc(const ast::FuncDeclStmt& fn) {
  // C ABI: exact symbol name, ExternalLinkage, no module prefix mangling.
  if (llvm::Function* existing = module_->getFunction(fn.name)) {
    existing->setCallingConv(llvm::CallingConv::C);
    public_funcs_[fn.name] = fn.name;
    concrete_funcs_[fn.name] = &fn;
    return true;
  }
  std::vector<llvm::Type*> params;
  for (const ast::Param& p : fn.params) {
    // Extern C: primitives / ptr / fun(...) all lower to C-compatible ABI types.
    // fun(T)->R is a raw function pointer (ptr), matching host C callbacks.
    params.push_back(typeFromName(p.type_name));
  }
  llvm::Type* ret_ty = typeFromName(fn.return_type.empty() ? "void" : fn.return_type);
  auto* fty = llvm::FunctionType::get(ret_ty, params, false);
  llvm::Function* lf =
      llvm::Function::Create(fty, llvm::Function::ExternalLinkage, fn.name, module_.get());
  lf->setCallingConv(llvm::CallingConv::C);
  public_funcs_[fn.name] = fn.name;
  concrete_funcs_[fn.name] = &fn;
  return true;
}

bool Codegen::isStringOperand(const ast::Expr& expr) const {
  if (expr.kind() == ast::ExprKind::StringLiteral) {
    return true;
  }
  if (expr.kind() == ast::ExprKind::Binary) {
    return isStringConcatBinary(static_cast<const ast::BinaryExpr&>(expr));
  }
  return exprTypeOf(expr) == "String";
}

bool Codegen::isStringConcatBinary(const ast::BinaryExpr& bin) const {
  if (bin.op != "+" || !bin.left || !bin.right) {
    return false;
  }
  auto side_stringy = [this](const ast::Expr& e) -> bool {
    if (e.kind() == ast::ExprKind::StringLiteral) {
      return true;
    }
    if (e.kind() == ast::ExprKind::Binary) {
      return isStringConcatBinary(static_cast<const ast::BinaryExpr&>(e));
    }
    // Avoid recursing into isStringOperand (which calls back here for Binary).
    return exprTypeOf(e) == "String";
  };
  // At least one side must be string-like so `1 + 2` stays arithmetic.
  return side_stringy(*bin.left) || side_stringy(*bin.right);
}

bool Codegen::isOwnedHeapClassExpr(const ast::Expr& expr) const {
  if (expr.kind() == ast::ExprKind::Heap) {
    return true;
  }
  // String `a + b` / interpolation lowers to a heap-owned String* (auto-free).
  if (expr.kind() == ast::ExprKind::Binary) {
    const auto& bin = static_cast<const ast::BinaryExpr&>(expr);
    if (isStringConcatBinary(bin)) {
      return true;
    }
  }
  return false;
}

bool Codegen::isArenaOwnedClassExpr(const ast::Expr& expr) const {
  if (expr.kind() != ast::ExprKind::MethodCall) {
    return false;
  }
  const auto& call = static_cast<const ast::MethodCallExpr&>(expr);
  if (!call.object || call.object->kind() != ast::ExprKind::Identifier) {
    return false;
  }
  const auto& id = static_cast<const ast::IdentifierExpr&>(*call.object);
  auto lit = local_types_.find(id.name);
  if (lit == local_types_.end() || lit->second != "Arena") {
    return false;
  }
  return call.method == "new_string" || call.method == "new";
}

// Static return type of `recv_ty.method` (instance methods + single delegate
// promotion). Used by chained-call type inference in exprTypeOf.
std::string Codegen::methodReturnTypeOn(const std::string& recv_ty,
                                        const std::string& method) const {
  if (recv_ty.empty()) {
    return {};
  }
  // Interface receivers: the signature lives on the interface declaration, not
  // on any concrete class. This is the single source of truth for interface
  // method typing — `exprTypeOf` and every other caller go through here.
  if (isInterfaceType(recv_ty)) {
    auto iit = interfaces_.find(recv_ty);
    if (iit == interfaces_.end()) {
      return {};
    }
    auto mit = iit->second.methods.find(method);
    if (mit == iit->second.methods.end() || !mit->second) {
      return {};
    }
    return resolveTypeName(mit->second->return_type);
  }
  const ClassInfo* info = findClass(recv_ty);
  if (!info || !info->ast) {
    return {};
  }
  for (const auto& m : info->ast->methods) {
    if (m && m->name == method) {
      return instantiateMemberType(m->return_type, *info);
    }
  }
  // [link:97-Implicit-Delegation]: unambiguous delegate promotion for typing.
  std::vector<const ClassInfo*> delegate_hits;
  for (const auto& field : info->ast->fields) {
    if (!field || !field->is_delegate || !isClassType(field->type_name)) {
      continue;
    }
    const ClassInfo* emb = findClass(field->type_name);
    if (!emb || !emb->ast) {
      continue;
    }
    for (const auto& m : emb->ast->methods) {
      if (m && m->name == method) {
        delegate_hits.push_back(emb);
        break;
      }
    }
  }
  if (delegate_hits.size() == 1) {
    for (const auto& m : delegate_hits[0]->ast->methods) {
      if (m && m->name == method) {
        return instantiateMemberType(m->return_type, *delegate_hits[0]);
      }
    }
  }
  return {};
}

// Return type of `ClassName.method(...)`: a real static method first, then an
// associated free function with the same name (e.g. `Variant.from_i32`).
std::string Codegen::staticMethodReturnType(const std::string& class_name,
                                            const std::string& method) const {
  const ClassInfo* cinfo = findClass(class_name);
  if (cinfo && cinfo->ast) {
    for (const auto& m : cinfo->ast->methods) {
      if (m && m->name == method && m->is_static) {
        return resolveTypeName(m->return_type);
      }
    }
  }
  auto fit = concrete_funcs_.find(method);
  if (fit == concrete_funcs_.end() || !fit->second) {
    return {};
  }
  return resolveTypeName(fit->second->return_type);
}

// Type produced by a destructuring `match` arm (`User { name } => name`).
// Array arms yield the element type of `Array<T>`; class arms yield the
// declared type of the bound field. Empty when the shape is not understood.
std::string Codegen::matchDestructureArmType(const ast::MatchBranch& br,
                                             const ast::MatchExpr& m) const {
  if (!br.destruct || !br.value || br.value->kind() != ast::ExprKind::Identifier) {
    return {};
  }
  const std::string& name = static_cast<const ast::IdentifierExpr&>(*br.value).name;

  if (br.destruct->kind == ast::MatchPatternKind::ArrayDestructure) {
    std::string abase;
    std::vector<std::string> aargs;
    const std::string sty = m.scrutinee ? exprTypeOf(*m.scrutinee) : std::string();
    if (splitGenericType(sty, abase, aargs) && abase == "Array" && aargs.size() == 1) {
      return aargs[0];
    }
    return {};
  }

  for (const ast::PatternField& pf : br.destruct->fields) {
    const std::string bind = pf.binding.empty() ? pf.name : pf.binding;
    if (bind != name) {
      continue;
    }
    const ClassInfo* ci = findClass(resolveTypeName(br.destruct->class_name));
    if (!ci) {
      return {};
    }
    auto fit = ci->field_index.find(physicalFieldName(pf.name));
    if (fit == ci->field_index.end() || fit->second >= ci->field_types.size()) {
      return {};
    }
    return ci->field_types[fit->second];
  }
  return {};
}

std::string Codegen::exprTypeOf(const ast::Expr& expr) const {
  switch (expr.kind()) {
    case ast::ExprKind::Match: {
      // Determine the result type from the arms. `some(x)` bindings are not in
      // `local_types_` during inference, so fall back to the payload type of the
      // scrutinee's Optional, and treat String literals as String.
      const auto& m = static_cast<const ast::MatchExpr&>(expr);
      const std::string scrut = m.scrutinee ? exprTypeOf(*m.scrutinee) : std::string();
      const bool scrut_opt = scrut.rfind("Optional<", 0) == 0;
      const std::string payload =
          scrut_opt ? scrut.substr(9, scrut.size() - 10) : std::string();
      std::string fallback;
      for (const ast::MatchBranch& br : m.branches) {
        if (!br.value) {
          continue;
        }
        if (br.value->kind() == ast::ExprKind::StringLiteral) {
          return "String";
        }
        // `some(x) => x` — the arm just forwards the payload.
        const bool is_binding =
            br.value->kind() == ast::ExprKind::Identifier && !br.binding.empty() &&
            static_cast<const ast::IdentifierExpr&>(*br.value).name == br.binding;
        const std::string destr_ty = matchDestructureArmType(br, m);
        const std::string ty =
            is_binding ? payload : (!destr_ty.empty() ? destr_ty : exprTypeOf(*br.value));
        if (ty.empty()) {
          continue;
        }
        if (ty != "ptr") {
          return ty;
        }
        if (fallback.empty()) {
          fallback = ty;
        }
      }
      return fallback.empty() ? "i32" : fallback;
    }
    case ast::ExprKind::IntLiteral:
      return "i32";
    case ast::ExprKind::FloatLiteral:
      return "f64";
    case ast::ExprKind::BoolLiteral:
      return "bool";
    case ast::ExprKind::StringLiteral:
      return "ptr";
    case ast::ExprKind::New:
      return static_cast<const ast::NewExpr&>(expr).class_name;
    case ast::ExprKind::This:
      return current_class_;
    case ast::ExprKind::Heap: {
      const auto& heap = static_cast<const ast::HeapExpr&>(expr);
      if (heap.inner) {
        return exprTypeOf(*heap.inner);
      }
      return {};
    }
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      if (id.name == "__FILE__" || id.name == "__FUNC__") {
        return "String";
      }
      if (id.name == "__LINE__") {
        return "i32";
      }
      auto lit = local_types_.find(id.name);
      if (lit != local_types_.end()) {
        return lit->second;
      }
      auto ct = const_types_.find(id.name);
      if (ct != const_types_.end()) {
        return ct->second;
      }
      auto gt = global_types_.find(id.name);
      if (gt != global_types_.end()) {
        return gt->second;
      }
      // Bare field name inside a method body (logical → physical age_).
      if (!current_class_.empty()) {
        const ClassInfo* info = findClass(current_class_);
        if (info) {
          auto fit = info->field_index.find(physicalFieldName(id.name));
          if (fit != info->field_index.end()) {
            return info->field_types[fit->second];
          }
        }
      }
      return {};
    }
    case ast::ExprKind::Coalesce: {
      const auto& co = static_cast<const ast::CoalesceExpr&>(expr);
      // `Optional<T> ?? T` yields T: prefer the declared payload of the left
      // side; fall back to the right operand when the left is not an Optional.
      if (co.left) {
        const std::string lt = exprTypeOf(*co.left);
        const std::string prefix = "Optional<";
        if (lt.rfind(prefix, 0) == 0 && lt.back() == '>') {
          return lt.substr(prefix.size(), lt.size() - prefix.size() - 1);
        }
      }
      return co.right ? exprTypeOf(*co.right) : std::string{};
    }
    case ast::ExprKind::Binary: {
      const auto& bin = static_cast<const ast::BinaryExpr&>(expr);
      if (isStringConcatBinary(bin)) {
        return "String";
      }
      if (bin.op == "&&" || bin.op == "||" || bin.op == "==" || bin.op == "!=" ||
          bin.op == "<" || bin.op == ">" || bin.op == "<=" || bin.op == ">=") {
        return "bool";
      }
      if (bin.left && bin.right &&
          (bin.op == "+" || bin.op == "-" || bin.op == "*" || bin.op == "/" ||
           bin.op == "%")) {
        const std::string lt = exprTypeOf(*bin.left);
        const std::string rt = exprTypeOf(*bin.right);
        std::string promoted = promoteNumericTypes(lt, rt);
        if (!promoted.empty()) {
          return promoted;
        }
      }
      return "i32";
    }
    case ast::ExprKind::Call: {
      const auto& call = static_cast<const ast::CallExpr&>(expr);
      if (call.callee == "arena") {
        return "Arena";
      }
      // Fat closure local invoke: payload fun type's return.
      if (fat_closure_locals_.count(call.callee) != 0) {
        auto lit = local_types_.find(call.callee);
        if (lit != local_types_.end()) {
          const std::string ft = resolveTypeName(lit->second);
          const auto arrow = ft.rfind("->");
          if (arrow != std::string::npos) {
            return ft.substr(arrow + 2);
          }
          return "void";
        }
      }
      // Plain function-pointer local invoke: `f := |x| x * 2` then `f(5)`.
      // Without this, inference falls through to the free-function lookup and
      // reports "cannot infer type" (regression seen by lambda_test).
      if (auto lit = local_types_.find(call.callee); lit != local_types_.end()) {
        const std::string ft = resolveTypeName(lit->second);
        if (isFunTypeName(ft)) {
          const auto arrow = ft.rfind("->");
          return arrow != std::string::npos ? ft.substr(arrow + 2) : std::string("void");
        }
      }
      auto it = concrete_funcs_.find(call.callee);
      if (it == concrete_funcs_.end() && !current_module_.empty()) {
        it = concrete_funcs_.find(current_module_ + "::" + call.callee);
      }
      if (it != concrete_funcs_.end() && it->second) {
        // `async fun` is called as a task handle; `await` then unwraps the payload.
        if (it->second->is_async) {
          return "ptr";
        }
        // Escaping closure returns a fat ABI, but the L type remains the payload fun(...).
        if (it->second->returns_fat_closure) {
          const std::string rt = resolveTypeName(it->second->return_type);
          if (rt == "fun()" || rt == "fun") {
            return "fun()->i32";
          }
          if (isFunTypeName(rt)) {
            return rt;
          }
          return "fun()->i32";
        }
        return resolveTypeName(it->second->return_type);
      }
      // Implicit `this`: a bare `name(...)` may be a method of the enclosing class.
      if (!current_class_.empty()) {
        const std::string t = methodReturnTypeOn(resolveTypeName(current_class_), call.callee);
        if (!t.empty()) {
          return t;
        }
      }
      return {};
    }
    case ast::ExprKind::MethodCall: {
      const auto& call = static_cast<const ast::MethodCallExpr&>(expr);
      // `obj?.m(...)`: the method is resolved on the Optional's payload type, and
      // the result is re-wrapped as `Optional<R>` (a method already returning an
      // Optional keeps its own type, so chaining never nests Optionals).
      const bool safe = call.is_safe;
      auto stripOptional = [](const std::string& ty) -> std::string {
        if (ty.rfind("Optional<", 0) == 0 && ty.size() > 10) {
          return ty.substr(9, ty.size() - 10);
        }
        return ty;
      };
      auto resolveOn = [&](const std::string& recv) -> std::string {
        if (recv.empty()) {
          return {};
        }
        // `?.` only short-circuits on an Optional receiver; on a concrete value
        // it degrades to a plain call and keeps the method's own return type.
        const bool shortCircuit = safe && recv.rfind("Optional<", 0) == 0;
        const std::string base = shortCircuit ? stripOptional(recv) : recv;
        if (base.empty()) {
          return {};
        }
        const std::string ret = methodReturnTypeOn(base, call.method);
        if (ret.empty()) {
          return {};
        }
        if (!shortCircuit) {
          return ret;
        }
        return ret.rfind("Optional<", 0) == 0 ? ret : "Optional<" + ret + ">";
      };
      // Bare method call `get(i)` inside a method body = implicit `this.get(i)`.
      if (!call.object) {
        if (current_class_.empty()) {
          return {};
        }
        return methodReturnTypeOn(resolveTypeName(current_class_), call.method);
      }
      // Receiver type: a bare identifier may name a class (static/assoc call),
      // otherwise take the expression's own static type. Chained calls such as
      // `v.as_string().unwrap()` resolve through the inner MethodCall.
      if (call.object->kind() != ast::ExprKind::Identifier) {
        return resolveOn(exprTypeOf(*call.object));
      }
      const auto& oid = static_cast<const ast::IdentifierExpr&>(*call.object);
      // `this.method(...)` inside a method body: resolve against the enclosing class.
      if (oid.name == "this" && !current_class_.empty()) {
        return methodReturnTypeOn(resolveTypeName(current_class_), call.method);
      }
      auto lit = local_types_.find(oid.name);
      // ---------------------------------------------------------------
      // [field receiver] `field.method(...)` inside a method body: fields are
      // not locals, so look the name up in the enclosing class before giving
      // up. Without this, `x := this.map.get(k)` infers `i32` (the fallback in
      // inferTypeFromExpr) and any later `opt.is_some()` fails codegen.
      // ---------------------------------------------------------------
      if (lit == local_types_.end() && !current_class_.empty()) {
        const ClassInfo* owner = findClass(current_class_);
        if (owner) {
          auto fit = owner->field_index.find(physicalFieldName(oid.name));
          if (fit != owner->field_index.end() && fit->second < owner->field_types.size()) {
            const std::string field_ty = resolveTypeName(owner->field_types[fit->second]);
            return resolveOn(field_ty);
          }
        }
      }
      // ClassName.staticMethod / Variant.from_i32 (free assoc) return types.
      if (lit == local_types_.end()) {
        if (isClassType(oid.name) || (const_cast<Codegen*>(this)->ensureTypeAvailable(oid.name) &&
                                      isClassType(oid.name))) {
          return staticMethodReturnType(oid.name, call.method);
        }
        return {};
      }
      if (lit->second == "Arena") {
        if (call.method == "alloc") {
          return "ptr";
        }
        if (call.method == "new_string") {
          return "String";
        }
        if (call.method == "new" && !call.args.empty() && call.args[0].value &&
            call.args[0].value->kind() == ast::ExprKind::New) {
          return static_cast<const ast::NewExpr&>(*call.args[0].value).class_name;
        }
        return {};
      }
      // Interfaces resolve via methodReturnTypeOn's interface branch.
      return resolveOn(lit->second);
    }
    case ast::ExprKind::Cast: {
      const auto& cast = static_cast<const ast::CastExpr&>(expr);
      if (!cast.value) {
        return {};
      }
      const std::string from_ty = exprTypeOf(*cast.value);
      if (isInterfaceType(from_ty) && isClassType(cast.type_name)) {
        return "Optional<" + cast.type_name + ">";
      }
      return cast.type_name;
    }
    case ast::ExprKind::Sizeof:
      return "i32";
    case ast::ExprKind::AddrOf:
      return "ptr";
    case ast::ExprKind::MemberAccess: {
      const auto& access = static_cast<const ast::MemberAccessExpr&>(expr);
      if (!access.object) {
        return {};
      }
      std::string obj_ty = exprTypeOf(*access.object);
      // `obj?.field` yields Optional<field> (flattened when the field is already
      // an Optional), so later links in the chain see the right receiver type.
      const bool safe = access.is_safe && obj_ty.rfind("Optional<", 0) == 0;
      if (safe) {
        obj_ty = obj_ty.substr(9, obj_ty.size() - 10);
      }
      if (!isClassType(obj_ty)) {
        return {};
      }
      const ClassInfo* info = findClass(obj_ty);
      if (!info) {
        return {};
      }
      const PropResolve pr = resolveProperty(*info, access.member, PropAccess::Read);
      std::string field_ty;
      if (pr.kind == PropResolveKind::Method && info->ast) {
        for (const auto& m : info->ast->methods) {
          if (m && m->name == pr.method_name) {
            field_ty = instantiateMemberType(m->return_type, *info);
            break;
          }
        }
      } else if (pr.kind == PropResolveKind::Field) {
        auto fit = info->field_index.find(pr.field_name);
        if (fit != info->field_index.end()) {
          field_ty = info->field_types[fit->second];
        }
      }
      if (field_ty.empty()) {
        return {};
      }
      if (!safe || field_ty.rfind("Optional<", 0) == 0) {
        return field_ty;
      }
      return "Optional<" + field_ty + ">";
    }
    case ast::ExprKind::Try: {
      const auto& try_expr = static_cast<const ast::TryExpr&>(expr);
      if (!try_expr.inner) {
        return {};
      }
      const std::string inner_ty = exprTypeOf(*try_expr.inner);
      std::string base;
      std::vector<std::string> args;
      if (!splitGenericType(inner_ty, base, args) || base != "Result" || args.size() != 2) {
        return {};
      }
      return args[0];
    }
    case ast::ExprKind::Spawn:
      return "ptr";
    case ast::ExprKind::Await: {
      // `await f()` yields f's declared return type (the task payload type),
      // even though `f()` itself is a `ptr` task handle.
      const auto& aw = static_cast<const ast::AwaitExpr&>(expr);
      if (!aw.task) {
        return {};
      }
      if (aw.task->kind() == ast::ExprKind::Call) {
        const auto& call = static_cast<const ast::CallExpr&>(*aw.task);
        auto it = concrete_funcs_.find(call.callee);
        if (it == concrete_funcs_.end() && !current_module_.empty()) {
          it = concrete_funcs_.find(current_module_ + "::" + call.callee);
        }
        if (it != concrete_funcs_.end() && it->second) {
          return resolveTypeName(it->second->return_type);
        }
      }
      return {};
    }
    case ast::ExprKind::ScopedCall: {
      const auto& sc = static_cast<const ast::ScopedCallExpr&>(expr);
      auto mit = module_exports_.find(expandAliasedScope(use_aliases_, sc.module));
      if (mit != module_exports_.end()) {
        auto fit = mit->second.find(sc.callee);
        if (fit != mit->second.end() && fit->second) {
          return resolveTypeName(fit->second->return_type);
        }
      }
      return {};
    }
    case ast::ExprKind::IncDec:
      return "i32";
    default:
      return {};
  }
}

bool Codegen::splitGenericType(const std::string& name, std::string& base,
                               std::vector<std::string>& args) {
  const auto lt = name.find('<');
  if (lt == std::string::npos || name.back() != '>') {
    return false;
  }
  base = name.substr(0, lt);
  args.clear();
  std::string inner = name.substr(lt + 1, name.size() - lt - 2);
  int depth = 0;
  std::string cur;
  for (char c : inner) {
    if (c == '<') {
      ++depth;
      cur.push_back(c);
    } else if (c == '>') {
      --depth;
      cur.push_back(c);
    } else if (c == ',' && depth == 0) {
      args.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) {
    args.push_back(cur);
  }
  return !base.empty() && !args.empty();
}

std::string Codegen::mangleSpecName(const std::string& base,
                                    const std::vector<std::string>& args) {
  std::string out = base;
  for (const std::string& a : args) {
    out.push_back('_');
    for (char c : a) {
      if (c == '<' || c == '>' || c == ',') {
        out.push_back('_');
      } else {
        out.push_back(c);
      }
    }
  }
  return out;
}

uint64_t Codegen::allocSizeOfTypeName(const std::string& type_name) {
  ensureDataLayout();
  const std::string resolved = resolveTypeName(type_name);
  llvm::Type* ty = llvmTypeForTypeName(resolved);
  if (ty->isVoidTy()) {
    return 0;
  }
  return module_->getDataLayout().getTypeAllocSize(ty);
}

bool Codegen::ensureTypeAvailable(const std::string& name) {
  const std::string resolved = resolveTypeName(name);
  if (resolved != name) {
    return ensureTypeAvailable(resolved);
  }
  // Reject over-nested declarations before specializing anything.
  if (const unsigned depth = genericNestingDepth(name); depth > kMaxGenericDepth) {
    error(lastTopLevelLoc(), "generic instantiation depth limit exceeded (" +
    // [A007] no AST loc available
                                 std::to_string(kMaxGenericDepth) + "): '" + name + "' nests " +
                                 std::to_string(depth) + " generic types");
    return false;
  }
  if (isFunTypeName(name)) {
    std::vector<std::string> params;
    std::string ret;
    bool has_ret = false;
    if (!parseFunTypeName(name, params, ret, has_ret)) {
      return false;
    }
    for (const std::string& p : params) {
      if (!ensureTypeAvailable(p)) {
        return false;
      }
    }
    if (has_ret && !ensureTypeAvailable(ret)) {
      return false;
    }
    return true;
  }
  if (name.empty() || name == "i32" || name == "i64" || name == "f64" || name == "bool" ||
      name == "void" || name == "ptr" || name == "Arena" || name == "String") {
    if (name == "String") {
      return isClassType("String");
    }
    return true;
  }
  if (isClassType(name) || isInterfaceType(name)) {
    return true;
  }
  std::string base;
  std::vector<std::string> args;
  if (!splitGenericType(name, base, args)) {
    return false;
  }
  for (const std::string& a : args) {
    if (!ensureTypeAvailable(a)) {
      return false;
    }
  }
  auto it = generic_class_templates_.find(base);
  if (it == generic_class_templates_.end()) {
    error(lastTopLevelLoc(), "unknown generic class template: " + base);
    // [A007] no AST loc available
    return false;
  }
  return specializeClass(*it->second, args, name);
}

// Purely syntactic nesting count: `i32` = 0, `Array<i32>` = 1,
// `Array<Optional<i32>>` = 2. Used to reject pathological declarations
// (`A<A<A<A<...>>>>`) before any specialization work starts.
unsigned Codegen::genericNestingDepth(const std::string& name) {
  std::string base;
  std::vector<std::string> args;
  if (!splitGenericType(name, base, args)) {
    return 0;
  }
  unsigned deepest = 0;
  for (const std::string& a : args) {
    deepest = std::max(deepest, genericNestingDepth(a));
  }
  return deepest + 1;
}

// Emit deferred method bodies for specializations first reached during the
// struct pass (their method declarations exist, bodies do not yet).
bool Codegen::drainPendingGenericBodies() {
  while (!pending_generic_bodies_.empty()) {
    std::string name = pending_generic_bodies_.back();
    pending_generic_bodies_.pop_back();
    ClassInfo* ninfo = findClassMut(name);
    if (!ninfo) {
      error(lastTopLevelLoc(), "lost specialization for " + name);
      // [A007] no AST loc available
      return false;
    }
    auto it = generic_class_templates_.find(ninfo->template_name);
    if (it == generic_class_templates_.end()) {
      error(lastTopLevelLoc(), "lost generic template for " + name);
      // [A007] no AST loc available
      return false;
    }
    for (auto& pending : pending_struct_bodies_) {
      if (pending.first == name) {
        ninfo->type->setBody(pending.second, /*isPacked=*/false);
        break;
      }
    }
    auto saved_subst = type_subst_;
    std::unordered_map<std::string, std::string> subst;
    for (size_t i = 0; i < it->second->generics.size() && i < ninfo->type_args.size(); ++i) {
      subst[it->second->generics[i].name] = ninfo->type_args[i];
    }
    type_subst_ = subst;
    bool bodies_ok = true;
    for (const auto& method : it->second->methods) {
      if (!method) {
        continue;
      }
      if (!emitMethodBody(name, *method)) {
        bodies_ok = false;
        break;
      }
    }
    type_subst_ = std::move(saved_subst);
    if (!bodies_ok) {
      return false;
    }
    ninfo->bodies_emitted = true;
  }
  return true;
}

bool Codegen::specializeClass(const ast::ClassDeclStmt& tmpl,
                              const std::vector<std::string>& args,
                              const std::string& instance_name) {
  if (classes_.count(instance_name) != 0) {
    return true;
  }
  // Over-nesting is rejected up front by ensureTypeAvailable's syntactic depth
  // check; no runtime depth counter is needed here.
  if (tmpl.generics.size() != args.size()) {
    error(tmpl.loc, "generic arity mismatch specializing " + tmpl.name);
    return false;
  }

  std::unordered_map<std::string, std::string> subst;
  for (size_t i = 0; i < tmpl.generics.size(); ++i) {
    subst[tmpl.generics[i].name] = args[i];
    if (!tmpl.generics[i].constraint.empty()) {
      if (!ensureTypeAvailable(args[i]) || !isClassType(args[i])) {
        error(tmpl.generics[i].loc,
              "constrained generic " + tmpl.generics[i].name + " requires a class type");
        return false;
      }
      if (!classImplements(args[i], tmpl.generics[i].constraint)) {
        error(tmpl.generics[i].loc, "Type '" + args[i] + "' does not implement interface '" +
                                        tmpl.generics[i].constraint + "'");
        return false;
      }
    }
  }

  auto saved_subst = type_subst_;
  type_subst_ = subst;

  ClassInfo info;
  info.ast = &tmpl;
  info.interfaces = tmpl.interfaces;
  info.type_args = args;
  info.template_name = tmpl.name;
  info.ir_name = mangleSpecName(tmpl.name, args);
  info.type = llvm::StructType::create(globalContext(), info.ir_name);

  // Register before filling fields so a nested specialization (`Map<T>` whose
  // methods mention `Optional<T>`) sees this type as known instead of recursing.
  classes_[instance_name] = std::move(info);
  ClassInfo* mut = findClassMut(instance_name);
  auto spec_failed = [this, &instance_name, &saved_subst]() {
    classes_.erase(instance_name);
    type_subst_ = std::move(saved_subst);
    return false;
  };

  std::vector<llvm::Type*> elements;
  unsigned idx = 0;
  for (const auto& field : tmpl.fields) {
    if (!field) {
      continue;
    }
    const std::string fty_name = resolveTypeName(field->type_name);
    if (isClassType(fty_name)) {
      if (!ensureTypeAvailable(fty_name)) {
        return spec_failed();
      }
    } else if (isInterfaceType(fty_name)) {
      // ok — fat pointer field
    }
    const std::string phys = physicalFieldName(field->name);
    mut->field_index[phys] = idx++;
    mut->field_types.push_back(fty_name);
    mut->field_is_pub[phys] = field->is_pub;
    if (isClassType(fty_name)) {
      elements.push_back(findClass(fty_name)->type);
    } else if (isInterfaceType(fty_name)) {
      elements.push_back(fatPtrTy());
    } else {
      elements.push_back(llvmTypeForTypeName(fty_name));
    }
  }
  mut->type->setBody(elements, /*isPacked=*/false);
  // A field may reference a class whose own body is still being built (e.g.
  // `Optional<T>` while `T` registers). Re-apply bodies once types settle.
  bool any_opaque = false;
  for (llvm::Type* e : elements) {
    auto* st = llvm::dyn_cast<llvm::StructType>(e);
    if (st && st->isOpaque()) {
      any_opaque = true;
      break;
    }
  }
  if (any_opaque) {
    pending_struct_bodies_.emplace_back(instance_name, std::move(elements));
  }

  ClassInfo* cinfo = mut;
  ensureTypeId(*cinfo);
  for (const auto& method : tmpl.methods) {
    if (!method) {
      continue;
    }
    if (cinfo->methods.count(method->name) != 0) {
      error(method->loc, "duplicate method in specialized class " + instance_name);
      type_subst_ = std::move(saved_subst);
      return false;
    }
    std::vector<llvm::Type*> params;
    if (!method->is_static) {
      params.push_back(ptrTy());  // implicit this*
    }
    for (const ast::Param& p : method->params) {
      params.push_back(abiTypeForParam(resolveTypeName(p.type_name)));
    }
    // Nested returns e.g. Optional<V> must be specialized under type_subst_.
    const std::string ret_name = resolveTypeName(method->return_type);
    if (!ret_name.empty() && ret_name != "void") {
      if (!ensureTypeAvailable(ret_name)) {
        type_subst_ = std::move(saved_subst);
        return false;
      }
    }
    llvm::Type* ret_ty = typeFromName(ret_name);
    auto* fty = llvm::FunctionType::get(ret_ty, params, false);
    const std::string mangled = mangleMethod(cinfo->ir_name, method->name);
    llvm::Function* fn =
        llvm::Function::Create(fty, llvm::Function::InternalLinkage, mangled, module_.get());
    fn->setCallingConv(llvm::CallingConv::C);
    if (!method->is_static && fn->arg_size() > 0) {
      fn->arg_begin()->setName("this");
    }
    cinfo->methods[method->name] = fn;
  }

  if (in_struct_pass_) {
    // Bodies must wait: a referenced struct body (e.g. `%CPropertys`) may still
    // be opaque, and any GEP into this instance would fail verification.
    if (std::find(pending_generic_bodies_.begin(), pending_generic_bodies_.end(),
                  instance_name) == pending_generic_bodies_.end()) {
      pending_generic_bodies_.push_back(instance_name);
    }
    type_subst_ = std::move(saved_subst);
    return true;
  }

  // A specialization first reached during the struct pass registered its method
  // declarations only: queue it so a later drain emits the bodies. Draining inline
  // here would insert those bodies into the caller's function at a bad point.
  if (!mut->bodies_emitted) {
    if (std::find(pending_generic_bodies_.begin(), pending_generic_bodies_.end(),
                  instance_name) == pending_generic_bodies_.end()) {
      pending_generic_bodies_.push_back(instance_name);
    }
  }

  // Specialization may run mid-function (e.g. first Array<i32> local); preserve IRBuilder.
  auto saved_named = named_values_;
  auto saved_locals = local_types_;
  auto saved_heap = heap_locals_;
  auto saved_owned_strings = owned_string_locals_;
  auto saved_readonly = readonly_refs_;
  auto saved_defers = defer_scopes_;
  auto saved_autofree = auto_free_scopes_;
  auto saved_class = current_class_;
  llvm::Value* saved_this = current_this_;
  const bool saved_static = current_method_is_static_;
  llvm::BasicBlock* saved_insert = builder_.GetInsertBlock();
  llvm::BasicBlock::iterator saved_point =
      saved_insert ? builder_.GetInsertPoint() : llvm::BasicBlock::iterator();

  // Keep type_subst_ while emitting method bodies so T resolves inside.
  bool ok = true;
  for (const auto& method : tmpl.methods) {
    if (!method) {
      continue;
    }
    if (!emitMethodBody(instance_name, *method)) {
      ok = false;
      break;
    }
  }
  if (ok) {
    mut->bodies_emitted = true;
  }

  named_values_ = std::move(saved_named);
  local_types_ = std::move(saved_locals);
  heap_locals_ = std::move(saved_heap);
  owned_string_locals_ = std::move(saved_owned_strings);
  readonly_refs_ = std::move(saved_readonly);
  defer_scopes_ = std::move(saved_defers);
  auto_free_scopes_ = std::move(saved_autofree);
  current_class_ = std::move(saved_class);
  current_this_ = saved_this;
  current_method_is_static_ = saved_static;
  if (saved_insert) {
    builder_.SetInsertPoint(saved_insert, saved_point);
  }
  type_subst_ = std::move(saved_subst);
  resettlePendingStructBodies();
  return ok;
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

void Codegen::error(const ast::SourceLocation& loc, const std::string& message) {
  had_error_ = true;
  // Any error leaves the IR in an undefined state; stop emitting so a broken
  // module (e.g. a by-value GEP on a not-yet-laid-out struct) never reaches
  // object emission and dies in the backend instead of printing a diagnostic.
  in_error_ = true;
  if (diags_) {
    diags_->error(loc, message);
  } else {
    std::cerr << "[ERROR] " << (loc.filename.empty() ? "<unknown>" : loc.filename) << ':'
              << loc.line << ':' << loc.column << " - " << message << '\n';
  }
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

llvm::Value* Codegen::toI32(llvm::Value* v, const ast::SourceLocation& loc) {
  if (!v) {
    return nullptr;
  }
  if (v->getType()->isIntegerTy(32)) {
    return v;
  }
  if (v->getType()->isIntegerTy(1)) {
    return builder_.CreateZExt(v, i32Ty(), "booltmp");
  }
  if (!v->getType()->isIntegerTy()) {
    // Never fabricate an integer from a struct/pointer: callers must convert
    // explicitly (a silent `sext ptr` breaks module verification).
    error(loc, "internal: cannot convert a non-integer value to i32");
    return llvm::ConstantInt::get(i32Ty(), 0);
  }
  return builder_.CreateIntCast(v, i32Ty(), /*isSigned=*/true, "casttmp");
}

// Widen/narrow a scalar to the declared storage type (i32 → i64 → f64).
llvm::Value* Codegen::coerceScalarTo(llvm::Value* v, llvm::Type* target) {
  if (!v || !target || v->getType() == target) {
    return v;
  }
  if (v->getType()->isIntegerTy() && target->isIntegerTy()) {
    return builder_.CreateIntCast(v, target, /*isSigned=*/true, "scalar.cast");
  }
  if (v->getType()->isIntegerTy() && target->isFloatingPointTy()) {
    return builder_.CreateSIToFP(v, target, "scalar.cast");
  }
  if (v->getType()->isFloatingPointTy() && target->isIntegerTy()) {
    return builder_.CreateFPToSI(v, target, "scalar.cast");
  }
  return v;
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
    // A previous statement terminated the block (return / guard clause); the
    // rest of this list is unreachable dead code, not part of the IR.
    if (builder_.GetInsertBlock() && builder_.GetInsertBlock()->getTerminator()) {
      break;
    }
    if (!emitStmt(*stmt)) {
      return false;
    }
  }
  return true;
}

void Codegen::enterDeferScope() {
  defer_scopes_.emplace_back();
  auto_free_scopes_.emplace_back();
}

void Codegen::noteAutoFreeLocal(const std::string& name) {
  if (auto_free_scopes_.empty()) {
    auto_free_scopes_.emplace_back();
  }
  auto_free_scopes_.back().push_back(name);
}

void Codegen::cancelAutoFreeLocal(const std::string& name) {
  for (auto sit = auto_free_scopes_.rbegin(); sit != auto_free_scopes_.rend(); ++sit) {
    auto& names = *sit;
    for (auto it = names.begin(); it != names.end();) {
      if (*it == name) {
        it = names.erase(it);
      } else {
        ++it;
      }
    }
  }
  arena_locals_.erase(name);
}

void Codegen::cancelAutoFreeEscaping(const ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      cancelAutoFreeLocal(id.name);
      // A raw-pointer alias escaping (`ptr p = obj as ptr` … `return p`) hands the
      // owning heap local to the caller as well.
      auto ait = ptr_aliases_.find(id.name);
      if (ait != ptr_aliases_.end()) {
        cancelAutoFreeLocal(ait->second);
      }
      break;
    }
    case ast::ExprKind::Cast: {
      const auto& cast = static_cast<const ast::CastExpr&>(expr);
      if (cast.value) {
        cancelAutoFreeEscaping(*cast.value);
      }
      break;
    }
    case ast::ExprKind::Heap: {
      const auto& heap = static_cast<const ast::HeapExpr&>(expr);
      if (heap.inner) {
        cancelAutoFreeEscaping(*heap.inner);
      }
      break;
    }
    default:
      break;
  }
}

void Codegen::noteExplicitLFreeInDefer(const ast::DeferStmt& stmt) {
  for (const ast::StmtPtr& s : stmt.body) {
    if (!s || s->kind() != ast::StmtKind::ExprStmt) {
      continue;
    }
    const auto& es = static_cast<const ast::ExprStmt&>(*s);
    if (!es.expr || es.expr->kind() != ast::ExprKind::Call) {
      continue;
    }
    const auto& call = static_cast<const ast::CallExpr&>(*es.expr);
    if (call.callee != "l_free" || call.args.size() != 1 || !call.args[0].value) {
      continue;
    }
    if (call.args[0].value->kind() != ast::ExprKind::Identifier) {
      continue;
    }
    const auto& id = static_cast<const ast::IdentifierExpr&>(*call.args[0].value);
    cancelAutoFreeLocal(id.name);
  }
}

bool Codegen::emitArrayOwnedCleanup(const std::string& name, const ClassInfo& info,
                                    const ast::SourceLocation& loc) {
  auto data_fit = info.field_index.find("data_");
  auto len_fit = info.field_index.find("length_");
  if (data_fit == info.field_index.end() || len_fit == info.field_index.end()) {
    error(loc, "Array cleanup missing data_/length_ on " + name);
    return false;
  }
  llvm::Function* free_fn = requireExtern("l_free", loc);
  if (!free_fn) {
    return false;
  }
  llvm::Value* arr = classObjectPtr(name, loc);
  if (!arr) {
    return false;
  }
  llvm::Value* data_gep =
      builder_.CreateStructGEP(info.type, arr, data_fit->second, name + ".data.ptr");
  llvm::Value* data = builder_.CreateLoad(ptrTy(), data_gep, name + ".data");
  llvm::Value* len_gep =
      builder_.CreateStructGEP(info.type, arr, len_fit->second, name + ".len.ptr");
  llvm::Value* len = builder_.CreateLoad(i32Ty(), len_gep, name + ".len");

  // Array<IFace>: each element is a fat pointer whose data_ptr was heap-escaped on push.
  if (!info.type_args.empty() && isInterfaceType(info.type_args[0])) {
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* idx_slot = createEntryAlloca(fn, i32Ty(), name + ".af.i");
    builder_.CreateStore(llvm::ConstantInt::get(i32Ty(), 0), idx_slot);

    llvm::BasicBlock* cond_bb =
        llvm::BasicBlock::Create(globalContext(), name + ".af.cond", fn);
    llvm::BasicBlock* body_bb =
        llvm::BasicBlock::Create(globalContext(), name + ".af.body", fn);
    llvm::BasicBlock* done_bb =
        llvm::BasicBlock::Create(globalContext(), name + ".af.done", fn);

    branchTo(cond_bb);
    builder_.SetInsertPoint(cond_bb);
    llvm::Value* i = builder_.CreateLoad(i32Ty(), idx_slot, name + ".af.i.ld");
    llvm::Value* cmp = builder_.CreateICmpSLT(i, len, name + ".af.cmp");
    builder_.CreateCondBr(cmp, body_bb, done_bb);

    builder_.SetInsertPoint(body_bb);
    llvm::Value* elem_ptr =
        builder_.CreateInBoundsGEP(fatPtrTy(), data, i, name + ".af.elem");
    llvm::Value* fat = builder_.CreateLoad(fatPtrTy(), elem_ptr, name + ".af.fat");
    llvm::Value* obj = builder_.CreateExtractValue(fat, 0, name + ".af.obj");
    llvm::Value* nonnull = builder_.CreateICmpNE(
        obj, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy())),
        name + ".af.nn");
    llvm::BasicBlock* free_bb =
        llvm::BasicBlock::Create(globalContext(), name + ".af.free", fn);
    llvm::BasicBlock* next_bb =
        llvm::BasicBlock::Create(globalContext(), name + ".af.next", fn);
    builder_.CreateCondBr(nonnull, free_bb, next_bb);

    builder_.SetInsertPoint(free_bb);
    builder_.CreateCall(free_fn, {obj});
    branchTo(next_bb);

    builder_.SetInsertPoint(next_bb);
    llvm::Value* next =
        builder_.CreateAdd(i, llvm::ConstantInt::get(i32Ty(), 1), name + ".af.inc");
    builder_.CreateStore(next, idx_slot);
    branchTo(cond_bb);

    builder_.SetInsertPoint(done_bb);
  }

  // Free the contiguous element buffer (l_realloc growth).
  llvm::Value* data_nn = builder_.CreateICmpNE(
      data, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy())),
      name + ".data.nn");
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* free_data_bb =
      llvm::BasicBlock::Create(globalContext(), name + ".free.data", fn);
  llvm::BasicBlock* after_data_bb =
      llvm::BasicBlock::Create(globalContext(), name + ".after.data", fn);
  builder_.CreateCondBr(data_nn, free_data_bb, after_data_bb);
  builder_.SetInsertPoint(free_data_bb);
  builder_.CreateCall(free_fn, {data});
  branchTo(after_data_bb);
  builder_.SetInsertPoint(after_data_bb);

  // Heap-allocated Array object itself (stack Arrays only free the buffer).
  if (heap_locals_.count(name) != 0) {
    auto vit = named_values_.find(name);
    if (vit == named_values_.end()) {
      error(loc, "auto-free unknown Array local: " + name);
      return false;
    }
    llvm::Value* obj = builder_.CreateLoad(ptrTy(), vit->second, name + ".arr.obj");
    builder_.CreateCall(free_fn, {obj});
  }
  return true;
}

bool Codegen::emitFatClosureCleanup(const std::string& name, const ast::SourceLocation& loc) {
  auto vit = named_values_.find(name);
  if (vit == named_values_.end()) {
    error(loc, "fat-closure cleanup missing state for '" + name + "'");
    return false;
  }
  llvm::Function* free_fn = requireExtern("l_free", loc);
  if (!free_fn) {
    return false;
  }
  // Escaping ctx stores capture values by value; freeing the ctx frees the promotions.
  llvm::Value* fat = builder_.CreateLoad(fatClosureTy(), vit->second, name + ".fat");
  llvm::Value* ctx = builder_.CreateExtractValue(fat, 1, name + ".ctx");
  llvm::Value* ctx_nn = builder_.CreateICmpNE(
      ctx, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy())),
      name + ".ctx.nn");
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* free_bb =
      llvm::BasicBlock::Create(globalContext(), name + ".fc.free", fn);
  llvm::BasicBlock* done_bb =
      llvm::BasicBlock::Create(globalContext(), name + ".fc.done", fn);
  builder_.CreateCondBr(ctx_nn, free_bb, done_bb);
  builder_.SetInsertPoint(free_bb);
  builder_.CreateCall(free_fn, {ctx});
  branchTo(done_bb);
  builder_.SetInsertPoint(done_bb);
  fat_closure_locals_.erase(name);
  fat_closure_ctx_tys_.erase(name);
  return true;
}

bool Codegen::emitLFreeOfLocal(const std::string& name, const ast::SourceLocation& loc) {
  if (fat_closure_locals_.count(name) != 0) {
    return emitFatClosureCleanup(name, loc);
  }
  auto lit = local_types_.find(name);
  if (lit != local_types_.end()) {
    const ClassInfo* info = findClass(lit->second);
    if (info && info->template_name == "Array") {
      return emitArrayOwnedCleanup(name, *info, loc);
    }
  }

  auto vit = named_values_.find(name);
  if (vit == named_values_.end()) {
    error(loc, "auto-free unknown local: " + name);
    return false;
  }
  llvm::Value* ptr = builder_.CreateLoad(ptrTy(), vit->second, name + ".autofree");
  if (arena_locals_.count(name) != 0) {
    llvm::Function* free_fn = requireExtern("l_arena_free", loc);
    if (!free_fn) {
      return false;
    }
    builder_.CreateCall(free_fn, {ptr});
    arena_locals_.erase(name);
    return true;
  }
  // Drop hook: run the class's `drop()` (if declared) before releasing memory.
  // The hook sees a valid object, so it can free owned buffers / key strings.
  if (lit != local_types_.end()) {
    const ClassInfo* info = findClass(lit->second);
    if (info) {
      auto dit = info->methods.find("drop");
      if (dit != info->methods.end() && dit->second && dit->second->arg_size() >= 1) {
        builder_.CreateCall(dit->second, {ptr});
      }
    }
  }
  // A heap String wrapper owns its `data_` only when that C string came from the
  // heap (l_str_concat / *_to_str). Literal-backed Strings point at static data.
  // Codegen records the owned set in `owned_string_locals_`.
  if (lit != local_types_.end() && lit->second == "String" &&
      owned_string_locals_.count(name) != 0) {
    const ClassInfo* sinfo = findClass("String");
    if (sinfo && sinfo->field_index.count("data_") != 0) {
      llvm::Value* data_gep = builder_.CreateStructGEP(
          sinfo->type, ptr, sinfo->field_index.at("data_"), name + ".data.ptr");
      llvm::Value* data = builder_.CreateLoad(ptrTy(), data_gep, name + ".data");
      llvm::Value* nn = builder_.CreateICmpNE(
          data, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy())),
          name + ".data.nn");
      llvm::Function* fn = builder_.GetInsertBlock()->getParent();
      llvm::BasicBlock* free_bb =
          llvm::BasicBlock::Create(globalContext(), name + ".data.free", fn);
      llvm::BasicBlock* done_bb =
          llvm::BasicBlock::Create(globalContext(), name + ".data.done", fn);
      builder_.CreateCondBr(nn, free_bb, done_bb);
      builder_.SetInsertPoint(free_bb);
      llvm::Function* lfree = requireExtern("l_free", loc);
      if (!lfree) {
        return false;
      }
      builder_.CreateCall(lfree, {data});
      branchTo(done_bb);
      builder_.SetInsertPoint(done_bb);
    }
  }
  llvm::Function* free_fn = requireExtern("l_free", loc);
  if (!free_fn) {
    return false;
  }
  builder_.CreateCall(free_fn, {ptr});
  return true;
}

bool Codegen::flushCurrentScopeAutoFrees() {
  if (auto_free_scopes_.empty()) {
    return true;
  }
  auto& names = auto_free_scopes_.back();
  for (auto it = names.rbegin(); it != names.rend(); ++it) {
    if (!emitLFreeOfLocal(*it, lastTopLevelLoc())) {
      return false;
    }
  }
  names.clear();
  return true;
}

bool Codegen::flushAllAutoFrees() {
  for (auto sit = auto_free_scopes_.rbegin(); sit != auto_free_scopes_.rend(); ++sit) {
    for (auto it = sit->rbegin(); it != sit->rend(); ++it) {
      if (!emitLFreeOfLocal(*it, lastTopLevelLoc())) {
        return false;
      }
    }
    sit->clear();
  }
  return true;
}

bool Codegen::flushHeldSyncLocks() {
  if (held_sync_locks_.empty()) {
    return true;
  }
  llvm::Function* unlock = requireExtern("l_mutex_unlock", lastTopLevelLoc());
  if (!unlock) {
    return false;
  }
  for (auto it = held_sync_locks_.rbegin(); it != held_sync_locks_.rend(); ++it) {
    if (*it) {
      builder_.CreateCall(unlock, {*it});
    }
  }
  held_sync_locks_.clear();
  return true;
}

bool Codegen::finishCleanups() {
  if (!flushAllDefers()) {
    return false;
  }
  if (!flushHeldSyncLocks()) {
    return false;
  }
  return flushAllAutoFrees();
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
    flushCurrentScopeAutoFrees();
  } else {
    if (!defer_scopes_.empty()) {
      defer_scopes_.back().clear();
    }
    if (!auto_free_scopes_.empty()) {
      auto_free_scopes_.back().clear();
    }
  }
  if (!defer_scopes_.empty()) {
    defer_scopes_.pop_back();
  }
  if (!auto_free_scopes_.empty()) {
    auto_free_scopes_.pop_back();
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
  } else {
    if (!defer_scopes_.empty()) {
      defer_scopes_.pop_back();
    }
    if (!auto_free_scopes_.empty()) {
      auto_free_scopes_.pop_back();
    }
  }
  return ok;
}

llvm::Value* Codegen::classObjectPtr(const std::string& name,
                                     const ast::SourceLocation& loc) {
  auto it = named_values_.find(name);
  if (it != named_values_.end()) {
    if (heap_locals_.count(name) != 0) {
      return builder_.CreateLoad(ptrTy(), it->second, name + ".obj");
    }
    return it->second;
  }
  // Closure / spawn capture: the ctx slot holds the parent local's address.
  auto cap = capture_slots_.find(name);
  if (cap != capture_slots_.end() && current_lambda_ctx_ && current_lambda_ctx_ty_) {
    llvm::Value* slot = builder_.CreateStructGEP(current_lambda_ctx_ty_, current_lambda_ctx_,
                                                 cap->second, name + ".cap");
    if (current_lambda_ctx_by_value_) {
      auto ty_it = capture_types_.find(name);
      llvm::Type* cty = llvmTypeForTypeName(
          ty_it != capture_types_.end() ? ty_it->second : "i32");
      llvm::Value* val = builder_.CreateLoad(cty, slot, name + ".capval");
      llvm::AllocaInst* tmp =
          createEntryAlloca(builder_.GetInsertBlock()->getParent(), cty, name + ".this");
      builder_.CreateStore(val, tmp);
      return tmp;
    }
    llvm::Value* local_ptr = builder_.CreateLoad(ptrTy(), slot, name + ".ptr");
    // Heap-promoted locals (drop-capable classes, `heap` / concat results) store
    // the object pointer in the slot; plain stack locals are the object itself.
    auto ty_it = capture_types_.find(name);
    const std::string cap_ty = ty_it != capture_types_.end() ? resolveTypeName(ty_it->second) : "";
    const ClassInfo* cap_info = cap_ty.empty() ? nullptr : findClass(cap_ty);
    const bool heap_captured = heap_locals_.count(name) != 0 || arena_locals_.count(name) != 0 ||
                               (cap_info && cap_info->methods.count("drop") != 0);
    if (heap_captured) {
      return builder_.CreateLoad(ptrTy(), local_ptr, name + ".obj");
    }
    return local_ptr;
  }
  auto git = global_vars_.find(name);
  if (git != global_vars_.end()) {
    auto gty = global_types_.find(name);
    if (gty != global_types_.end() && isClassType(gty->second)) {
      return git->second;  // GlobalVariable* decays to i8*/Class* address
    }
  }
  error(loc, "unknown variable: " + name);
  return nullptr;
}

Codegen::PropResolve Codegen::resolveProperty(const ClassInfo& info, const std::string& prop,
                                              PropAccess access) const {
  PropResolve out;
  // Trailing `_` identifiers are forbidden in source
  // ([link:69-Banned-Trailing-Underscores-Field-Syntax]); bare `_` is not a property.
  if (prop.size() > 1 && prop.back() == '_') {
    return out;
  }

  // Property sugar: prefer get_x / set_x.
  //
  // Exception — the accessor's own body. Inside `set_x`, `this.x = v` must
  // write the physical field `x_`: going through sugar again would re-enter
  // `set_x`, i.e. infinite recursion (the same for `get_x` reading `x`).
  const std::string phys_first = physicalFieldName(prop);
  const bool has_field = info.field_index.count(phys_first) != 0;
  const std::string method =
      (access == PropAccess::Write) ? ("set_" + prop) : ("get_" + prop);
  const bool in_own_accessor = (current_fn_name_ == method);
  if (!in_own_accessor && info.methods.count(method) != 0) {
    out.kind = PropResolveKind::Method;
    out.method_name = method;
    return out;
  }

  // Fallback: hidden physical field `prop_` (any visibility).
  if (has_field) {
    out.kind = PropResolveKind::Field;
    out.field_name = phys_first;
    return out;
  }
  return out;
}

llvm::Value* Codegen::classValuePtr(const ast::Expr& expr, const std::string& class_name) {
  if (!ensureTypeAvailable(class_name)) {
    error(expr.loc, "unknown class: " + class_name);
    return nullptr;
  }
  const ClassInfo* info = findClass(class_name);
  if (!info) {
    error(expr.loc, "unknown class: " + class_name);
    return nullptr;
  }
  if (expr.kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
    return classObjectPtr(id.name, id.loc);
  }
  llvm::Value* v = emitExpr(expr);
  if (!v) {
    return nullptr;
  }
  if (v->getType()->isPointerTy()) {
    return v;
  }
  if (v->getType() == info->type) {
    llvm::Function* cur = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, class_name + ".tmp");
    builder_.CreateStore(v, tmp);
    return tmp;
  }
  error(expr.loc, "expected class value of type " + class_name);
  return nullptr;
}

bool Codegen::registerInterface(const ast::InterfaceDeclStmt& decl) {
  if (interfaces_.count(decl.name) != 0) {
    error(decl.loc, "redefinition of interface: " + decl.name);
    return false;
  }
  InterfaceInfo info;
  // Slot 0 is TypeID; methods occupy 1..N.
  unsigned idx = 1;
  for (const auto& method : decl.methods) {
    if (!method) {
      continue;
    }
    if (info.method_index.count(method->name) != 0) {
      error(method->loc, "duplicate method in interface " + decl.name + ": " + method->name);
      return false;
    }
    info.method_names.push_back(method->name);
    info.method_index[method->name] = idx++;
    info.methods[method->name] = method.get();
  }
  interfaces_[decl.name] = std::move(info);
  return true;
}

llvm::GlobalVariable* Codegen::ensureTypeId(ClassInfo& info) {
  if (info.type_id) {
    return info.type_id;
  }
  const std::string name =
      info.ir_name.empty() ? "typeid_anon" : ("typeid_" + info.ir_name);
  // Unique address of a private i8 global is the TypeID (compared via icmp eq).
  info.type_id = new llvm::GlobalVariable(
      *module_, llvm::Type::getInt8Ty(globalContext()), /*isConstant=*/true,
      llvm::GlobalValue::InternalLinkage,
      llvm::ConstantInt::get(llvm::Type::getInt8Ty(globalContext()), 0), name);
  return info.type_id;
}

llvm::ArrayType* Codegen::vtableArrayTy(const InterfaceInfo& iface) {
  // [TypeID, ...methods]
  return llvm::ArrayType::get(ptrTy(), 1 + iface.method_names.size());
}

bool Codegen::emitClassStruct(const ast::ClassDeclStmt& decl) {
  if (!decl.generics.empty()) {
    return true;  // template — specialized on demand
  }
  if (classes_.count(decl.name) != 0) {
    error(decl.loc, "redefinition of class: " + decl.name);
    return false;
  }

  ClassInfo info;
  info.ir_name = mangleName(decl.module_name, decl.name, decl.is_pub);
  info.type = llvm::StructType::create(globalContext(), info.ir_name);
  info.interfaces = decl.interfaces;
  info.ast = &decl;

  // Register the (still opaque) struct BEFORE walking fields. A self-recursive
  // shape like `Map<String, CPropertys>` inside `CPropertys` would otherwise
  // recurse into specialization while this class is unknown, silently dropping
  // the module. LLVM structs are identified types, so a later `setBody` is fine
  // and the recursive reference stays a pointer-sized slot.
  classes_[decl.name] = std::move(info);
  auto register_failed = [this, &decl]() {
    classes_.erase(decl.name);
    return false;
  };
  ClassInfo* self = findClassMut(decl.name);

  std::vector<llvm::Type*> elements;
  unsigned idx = 0;
  for (const auto& field : decl.fields) {
    if (!field) {
      continue;
    }
    const std::string phys = physicalFieldName(field->name);
    if (self->field_index.count(phys) != 0) {
      error(field->loc, "duplicate field '" + field->name + "' in class " + decl.name);
      return register_failed();
    }
    const std::string fty_name = field->type_name;
    // A field may name a generic instantiation (`Map<Variant>`) nobody has
    // materialized yet — materialize it so the field embeds the real struct.
    if (fty_name.find('<') != std::string::npos && !isClassType(fty_name)) {
      if (!ensureTypeAvailable(fty_name)) {
        return register_failed();
      }
    }
    if (isClassType(fty_name)) {
      if (!ensureTypeAvailable(fty_name)) {
        return register_failed();
      }
      // A still-opaque dependency cannot be embedded yet; lay this class out
      // once the dependency's body is settled (see pending_struct_bodies_).
      const ClassInfo* ft = findClass(fty_name);
      if (!ft || !ft->type || ft->type->isOpaque()) {
        deferred_bodies_.push_back(&decl);
        classes_.erase(decl.name);
        return true;
      }
    }
    llvm::Type* fty = nullptr;
    if (isClassType(fty_name)) {
      fty = findClass(fty_name)->type;
    } else if (isInterfaceType(fty_name)) {
      fty = fatPtrTy();
    } else {
      fty = llvmTypeForTypeName(fty_name);
    }
    self->field_index[phys] = idx++;
    self->field_types.push_back(fty_name);
    self->field_is_pub[phys] = field->is_pub;
    elements.push_back(fty);
  }
  self->type->setBody(elements, /*isPacked=*/false);
  ensureTypeId(*self);
  resettlePendingStructBodies();
  return true;
}

// Re-applies field lists captured while a referenced class was still opaque.
// Cheap (no new types), and keeps recursive generic shapes sized.
void Codegen::resettlePendingStructBodies() {
  std::vector<std::pair<std::string, std::vector<llvm::Type*>>> still_pending;
  for (auto& pending : pending_struct_bodies_) {
    bool all_sized = true;
    for (llvm::Type* e : pending.second) {
      if (auto* st = llvm::dyn_cast<llvm::StructType>(e)) {
        if (st->isOpaque()) {
          all_sized = false;
          break;
        }
      }
    }
    ClassInfo* info = findClassMut(pending.first);
    if (!info || !info->type) {
      continue;
    }
    if (!all_sized) {
      still_pending.push_back(std::move(pending));
      continue;
    }
    info->type->setBody(pending.second, /*isPacked=*/false);
  }
  pending_struct_bodies_ = std::move(still_pending);
}

bool Codegen::declareClassMethods(const ast::ClassDeclStmt& decl) {
  if (!decl.generics.empty()) {
    return true;
  }
  ClassInfo* info = findClassMut(decl.name);
  if (!info) {
    return false;
  }
  for (const auto& method : decl.methods) {
    if (!method) {
      continue;
    }
    if (info->methods.count(method->name) != 0) {
      error(method->loc, "duplicate method in class " + decl.name + ": " + method->name);
      return false;
    }
    std::vector<llvm::Type*> params;
    if (!method->is_static) {
      params.push_back(ptrTy());  // implicit this*
    }
    for (const ast::Param& p : method->params) {
      params.push_back(abiTypeForParam(p.type_name));
    }
    // Specialize nested returns e.g. Optional<i32> before reading the LLVM type.
    if (!method->return_type.empty() && method->return_type != "void") {
      if (!ensureTypeAvailable(method->return_type)) {
        error(method->loc, "cannot resolve return type '" + method->return_type + "' of " + decl.name + "." +
              method->name);
        return false;
      }
    }
    llvm::Type* ret_ty = typeFromName(method->return_type);
    auto* fty = llvm::FunctionType::get(ret_ty, params, false);
    const std::string mangled = mangleMethod(info->ir_name, method->name);
    llvm::Function* fn =
        llvm::Function::Create(fty, llvm::Function::InternalLinkage, mangled, module_.get());
    fn->setCallingConv(llvm::CallingConv::C);
    if (!method->is_static && fn->arg_size() > 0) {
      fn->arg_begin()->setName("this");
    }
    info->methods[method->name] = fn;
  }
  return true;
}

// [link:97-Implicit-Delegation] vtable side: makes `method` callable on
// `decl`'s objects by forwarding to the embedded delegate that defines it.
//
// A delegate's method takes the *embedded* object as `this`, but an interface
// slot is called with the outer object, so a thin forwarder is needed:
//   Outer_method(Outer* this, args...) -> Inner_method(&this->Inner, args...)
// The forwarder is cached in `info->methods`, exactly like a declared method,
// so the vtable, dispatch, and property resolution all see one regular entry.
bool Codegen::ensureDelegateMethod(const ast::ClassDeclStmt& decl,
                                   const std::string& method) {
  ClassInfo* info = findClassMut(decl.name);
  if (!info || !info->type) {
    return false;
  }
  if (info->methods.count(method) != 0) {
    return true;
  }
  // One unambiguous delegate may provide it; two is an ambiguity, not a pick.
  const ast::FuncDeclStmt* hit_ast = nullptr;
  const ClassInfo* hit_info = nullptr;
  unsigned hit_field = 0;
  for (const auto& field : info->ast->fields) {
    if (!field || !field->is_delegate) {
      continue;
    }
    if (!ensureTypeAvailable(field->type_name) || !isClassType(field->type_name)) {
      return false;
    }
    const ClassInfo* emb = findClass(field->type_name);
    if (!emb) {
      return false;
    }
    const ast::FuncDeclStmt* m = nullptr;
    for (const auto& cand : emb->ast->methods) {
      if (cand && cand->name == method) {
        m = cand.get();
        break;
      }
    }
    if (!m) {
      continue;
    }
    if (hit_ast) {
      return false;  // ambiguous across delegates: not a promoted implementation
    }
    const auto fit = info->field_index.find(physicalFieldName(field->name));
    if (fit == info->field_index.end()) {
      return false;
    }
    hit_ast = m;
    hit_info = emb;
    hit_field = fit->second;
  }
  if (!hit_ast || !hit_info) {
    return false;
  }

  std::vector<llvm::Type*> params;
  if (!hit_ast->is_static) {
    params.push_back(ptrTy());
  }
  for (const ast::Param& p : hit_ast->params) {
    params.push_back(abiTypeForParam(p.type_name));
  }
  if (!hit_ast->return_type.empty() && hit_ast->return_type != "void") {
    if (!ensureTypeAvailable(hit_ast->return_type)) {
      return false;
    }
  }
  llvm::Type* ret_ty = typeFromName(hit_ast->return_type);
  auto* fty = llvm::FunctionType::get(ret_ty, params, false);
  llvm::Function* fn = llvm::Function::Create(
      fty, llvm::Function::InternalLinkage,
      mangleMethod(info->ir_name, "delegate_" + hit_info->ir_name + "_" + method),
      module_.get());
  fn->setCallingConv(llvm::CallingConv::C);
  if (!hit_ast->is_static) {
    fn->arg_begin()->setName("this");
  }

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  auto pit = hit_info->methods.find(method);
  if (pit == hit_info->methods.end() || !pit->second) {
    fn->eraseFromParent();
    return false;
  }
  std::vector<llvm::Value*> args;
  if (!hit_ast->is_static) {
    llvm::Value* self = &*fn->arg_begin();
    args.push_back(
        builder_.CreateStructGEP(info->type, self, hit_field, "delegate.this"));
  }
  for (unsigned i = hit_ast->is_static ? 0 : 1; i < fn->arg_size(); ++i) {
    args.push_back(fn->getArg(i));
  }
  llvm::CallInst* call = builder_.CreateCall(pit->second, args);
  if (ret_ty->isVoidTy()) {
    builder_.CreateRetVoid();
  } else {
    builder_.CreateRet(call);
  }
  info->methods[method] = fn;
  return true;
}

bool Codegen::buildVtables(const ast::ClassDeclStmt& decl) {  if (!decl.generics.empty()) {
    return true;
  }
  ClassInfo* info = findClassMut(decl.name);
  if (!info) {
    return false;
  }
  llvm::GlobalVariable* tid = ensureTypeId(*info);
  for (const std::string& iface_name : decl.interfaces) {
    const InterfaceInfo* iface = findInterface(iface_name);
    if (!iface) {
      error(decl.loc, "class " + decl.name + " implements unknown interface " + iface_name);
      return false;
    }
    std::vector<llvm::Constant*> slots;
    slots.push_back(tid);  // index 0: TypeID (unique address)
    for (const std::string& method_name : iface->method_names) {
      auto mit = info->methods.find(method_name);
      if (mit == info->methods.end()) {
        // [link:97-Implicit-Delegation]: a class may satisfy the interface
        // through an embedded (`delegate`) field. The slot is the delegated
        // class's function — vtable entry N of the outer vtable is executed as
        // `this = the delegate field's address` by delegate-aware dispatch.
        if (!ensureDelegateMethod(decl, method_name)) {
          error(decl.loc, "class " + decl.name + " missing method '" + method_name +
                "' for interface " + iface_name);
          return false;
        }
        mit = info->methods.find(method_name);
        if (mit == info->methods.end()) {
          error(decl.loc, "class " + decl.name + " missing method '" + method_name +
                "' for interface " + iface_name);
          return false;
        }
      }
      slots.push_back(mit->second);
    }
    llvm::ArrayType* arr_ty = vtableArrayTy(*iface);
    llvm::Constant* init = llvm::ConstantArray::get(arr_ty, slots);
    auto* gv = new llvm::GlobalVariable(
        *module_, arr_ty, /*isConstant=*/true, llvm::GlobalValue::InternalLinkage, init,
        vtableName(decl.name, iface_name));
    info->vtables[iface_name] = gv;
  }
  return true;
}

bool Codegen::emitAutoCloneBody(const std::string& class_name, llvm::Function* fn,
                                const ast::SourceLocation& loc) {
  ClassInfo* info = findClassMut(class_name);
  if (!info || !info->type) {
    error(loc, "auto clone: unknown class " + class_name);
    return false;
  }
  llvm::Value* self = &*fn->arg_begin();
  // Phase 1: treat the whole object as a value — load/store is a bitwise copy.
  // Nested class fields embedded by value are copied; raw ptr / iface slots stay shallow.
  llvm::Value* copy = builder_.CreateLoad(info->type, self, "clone.copy");
  if (!fn->getReturnType()->isStructTy()) {
    error(loc, "auto clone return type must be the class value for " + class_name);
    return false;
  }
  builder_.CreateRet(copy);
  return true;
}

llvm::Value* Codegen::makeStringLitObject(const std::string& text, const std::string& name,
                                          const ast::SourceLocation& loc) {
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error(loc, "String type not available for to_string");
    return nullptr;
  }
  auto init_it = info->methods.find("init");
  if (init_it == info->methods.end()) {
    error(loc, "String.init not found for to_string");
    return nullptr;
  }
  llvm::Function* cur = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, name);
  for (size_t i = 0; i < info->field_types.size(); ++i) {
    llvm::Type* fty = llvmTypeForTypeName(info->field_types[i]);
    llvm::Value* gep =
        builder_.CreateStructGEP(info->type, tmp, static_cast<unsigned>(i), name + ".z");
    builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
  }
  llvm::Value* cstr = builder_.CreateGlobalString(text, name + ".cstr");
  builder_.CreateCall(init_it->second, {tmp, cstr});
  return tmp;
}

llvm::Value* Codegen::concatStringObjects(llvm::Value* lhs, llvm::Value* rhs,
                                          const ast::SourceLocation& loc) {
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error(loc, "String type not available for to_string concat");
    return nullptr;
  }
  auto mit = info->methods.find("concat");
  if (mit == info->methods.end()) {
    error(loc, "String.concat not found for to_string");
    return nullptr;
  }
  llvm::Value* by_val = builder_.CreateCall(mit->second, {lhs, rhs}, "ts.concat");
  llvm::Function* cur = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, "ts.concat.tmp");
  builder_.CreateStore(by_val, tmp);
  return tmp;
}

bool Codegen::emitAutoToStringBody(const std::string& class_name, llvm::Function* fn,
                                   const ast::SourceLocation& loc) {
  ClassInfo* info = findClassMut(class_name);
  if (!info || !info->type) {
    error(loc, "auto to_string: unknown class " + class_name);
    return false;
  }
  if (!ensureTypeAvailable("String")) {
    error(loc, "auto to_string requires String");
    return false;
  }
  const ClassInfo* str_info = findClass("String");
  if (!str_info) {
    error(loc, "String type not available for auto to_string");
    return false;
  }
  if (!fn->getReturnType()->isStructTy() || fn->getReturnType() != str_info->type) {
    error(loc, "auto to_string must return String by value");
    return false;
  }

  llvm::Value* self = &*fn->arg_begin();
  const std::string display =
      !info->template_name.empty() ? info->template_name
                                   : (info->ast ? info->ast->name : class_name);

  llvm::Value* acc = makeStringLitObject(display + " { ", "ts.head", loc);
  if (!acc) {
    return false;
  }

  // Walk fields in declaration order (field_index values are sequential).
  std::vector<std::pair<unsigned, std::string>> ordered;
  ordered.reserve(info->field_types.size());
  for (const auto& kv : info->field_index) {
    ordered.emplace_back(kv.second, kv.first);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  for (size_t fi = 0; fi < ordered.size(); ++fi) {
    const unsigned idx = ordered[fi].first;
    const std::string& fname = ordered[fi].second;
    const std::string& fty = info->field_types[idx];

    if (fi > 0) {
      llvm::Value* sep = makeStringLitObject(", ", "ts.sep", loc);
      if (!sep) {
        return false;
      }
      acc = concatStringObjects(acc, sep, loc);
      if (!acc) {
        return false;
      }
    }

    const std::string logical = displayFieldName(fname);
    llvm::Value* label = makeStringLitObject(logical + ": ", "ts.lab", loc);
    if (!label) {
      return false;
    }
    acc = concatStringObjects(acc, label, loc);
    if (!acc) {
      return false;
    }

    llvm::Value* gep =
        builder_.CreateStructGEP(info->type, self, idx, fname + ".ts");
    llvm::Value* piece = nullptr;
    if (fty == "i32") {
      llvm::Value* v = builder_.CreateLoad(i32Ty(), gep, fname + ".v");
      piece = emitIntToStringObject(v);
    } else if (fty == "i64") {
      llvm::Value* v = builder_.CreateLoad(i64Ty(), gep, fname + ".v");
      llvm::Value* as_i32 =
          builder_.CreateIntCast(v, i32Ty(), /*isSigned=*/true, fname + ".i32");
      piece = emitIntToStringObject(as_i32);
    } else if (fty == "bool") {
      llvm::Value* v = builder_.CreateLoad(i1Ty(), gep, fname + ".v");
      // Select "true" / "false" as String objects.
      llvm::Value* t_obj = makeStringLitObject("true", "ts.true", loc);
      llvm::Value* f_obj = makeStringLitObject("false", "ts.false", loc);
      if (!t_obj || !f_obj) {
        return false;
      }
      // Both are String*; pick pointer via select.
      piece = builder_.CreateSelect(v, t_obj, f_obj, "ts.bool");
    } else if (fty == "String") {
      // Embedded String by value — address is a String*.
      piece = gep;
    } else if (fty == "f64") {
      piece = makeStringLitObject("<f64>", "ts.f64", loc);
    } else if (fty == "ptr") {
      piece = makeStringLitObject("<ptr>", "ts.ptr", loc);
    } else {
      // Nested class / interface / unknown — placeholder.
      piece = makeStringLitObject("<Object>", "ts.obj", loc);
    }
    if (!piece) {
      return false;
    }
    acc = concatStringObjects(acc, piece, loc);
    if (!acc) {
      return false;
    }
  }

  llvm::Value* tail = makeStringLitObject(" }", "ts.tail", loc);
  if (!tail) {
    return false;
  }
  acc = concatStringObjects(acc, tail, loc);
  if (!acc) {
    return false;
  }

  llvm::Value* ret = builder_.CreateLoad(str_info->type, acc, "ts.ret");
  builder_.CreateRet(ret);
  return true;
}

bool Codegen::emitMethodBody(const std::string& class_name, const ast::FuncDeclStmt& method) {
  ClassInfo* info = findClassMut(class_name);
  if (!info) {
    return false;
  }
  llvm::Function* fn = info->methods[method.name];
  if (!fn || !fn->empty()) {
    error(method.loc, "bad method function for " + class_name + "." + method.name);
    return false;
  }

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  named_values_.clear();
  local_types_.clear();
  heap_locals_.clear();
  owned_string_locals_.clear();
  arena_locals_.clear();
  readonly_refs_.clear();
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  current_fn_return_type_ = resolveTypeName(method.return_type);
  current_fn_name_ = method.name;
  enterDeferScope();

  current_class_ = class_name;
  current_method_is_static_ = method.is_static;
  current_this_ = method.is_static ? nullptr : &*fn->arg_begin();

  if (method.is_compiler_generated && method.name == "clone") {
    const bool ok = emitAutoCloneBody(class_name, fn, method.loc);
    defer_scopes_.clear();
    auto_free_scopes_.clear();
    current_class_.clear();
    current_this_ = nullptr;
    current_method_is_static_ = false;
    current_fn_return_type_.clear();
    current_fn_name_.clear();
    if (!ok) {
      return false;
    }
    if (llvm::verifyFunction(*fn, &llvm::errs())) {
      error(method.loc, "method verification failed: " + class_name + "." + method.name);
      fn->eraseFromParent();
      return false;
    }
    return true;
  }

  if (method.is_compiler_generated && method.name == "to_string") {
    const bool ok = emitAutoToStringBody(class_name, fn, method.loc);
    defer_scopes_.clear();
    auto_free_scopes_.clear();
    current_class_.clear();
    current_this_ = nullptr;
    current_method_is_static_ = false;
    current_fn_return_type_.clear();
    if (!ok) {
      return false;
    }
    if (llvm::verifyFunction(*fn, &llvm::errs())) {
      error(method.loc, "method verification failed: " + class_name + "." + method.name);
      fn->eraseFromParent();
      return false;
    }
    return true;
  }

  unsigned idx = method.is_static ? 0u : 1u;
  for (const ast::Param& param : method.params) {
    llvm::Argument& arg = *std::next(fn->arg_begin(), static_cast<int>(idx));
    arg.setName(param.name);
    bindParam(fn, &arg, param, resolveTypeName(param.type_name));
    ++idx;
  }

  if (!emitStmtList(method.body)) {
    current_class_.clear();
    current_this_ = nullptr;
    current_method_is_static_ = false;
    current_fn_return_type_.clear();
    return false;
  }

  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    if (!finishCleanups()) {
      current_class_.clear();
      current_this_ = nullptr;
      current_method_is_static_ = false;
      current_fn_return_type_.clear();
      return false;
    }
    if (fn->getReturnType()->isVoidTy()) {
      builder_.CreateRetVoid();
    } else if (fn->getReturnType()->isIntegerTy(32)) {
      builder_.CreateRet(llvm::ConstantInt::get(i32Ty(), 0));
    } else {
      error(method.loc, "missing return in method " + class_name + "." + method.name);
      current_class_.clear();
      current_this_ = nullptr;
      current_method_is_static_ = false;
      current_fn_return_type_.clear();
      return false;
    }
  }
  defer_scopes_.clear();
  auto_free_scopes_.clear();

  current_class_.clear();
  current_this_ = nullptr;
  current_method_is_static_ = false;
  current_fn_return_type_.clear();

  if (llvm::verifyFunction(*fn, &llvm::errs())) {
    error(method.loc, "method verification failed: " + class_name + "." + method.name);
    fn->eraseFromParent();
    return false;
  }
  return true;
}

bool Codegen::emitClassMethodBodies(const ast::ClassDeclStmt& decl) {
  if (!decl.generics.empty()) {
    return true;
  }
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
  in_error_ = false;
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ExternModule) {
      continue;
    }
    const auto& ext = static_cast<const ast::ExternModuleStmt&>(*stmt);
    for (const auto& decl : ext.decls) {
    }
  }
  // `-g`: create the compile unit first; every emitted function then attaches a
  // DISubprogram, and emitObjectFile() finalizes the builder before codegen.
  if (debug_info_ && !di_builder_) {
    initDebugInfo(module_->getModuleIdentifier());
  }
  named_values_.clear();
  local_types_.clear();
  const_values_.clear();
  const_types_.clear();
  global_vars_.clear();
  global_types_.clear();
  global_readonly_.clear();
  pending_global_class_inits_.clear();
  closure_locals_.clear();
  fat_closure_locals_.clear();
  fat_closure_ctx_tys_.clear();
  capture_slots_.clear();
  capture_types_.clear();
  current_lambda_ctx_ = nullptr;
  current_lambda_ctx_ty_ = nullptr;
  current_lambda_ctx_by_value_ = false;
  pending_closure_ctx_ = nullptr;
  pending_closure_ctx_ty_ = nullptr;
  lambda_ctx_seq_ = 0;
  deferred_bodies_.clear();
  classes_.clear();
  interfaces_.clear();
  generic_templates_.clear();
  concrete_funcs_.clear();
  generic_class_templates_.clear();
  specialized_funcs_.clear();
  type_subst_.clear();
  heap_locals_.clear();
  owned_string_locals_.clear();
  arena_locals_.clear();
  readonly_refs_.clear();
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  public_funcs_.clear();
  private_funcs_.clear();
  module_exports_.clear();
  use_aliases_.clear();
  current_module_.clear();
  fat_ptr_ty_ = nullptr;
  current_class_.clear();
  current_this_ = nullptr;
  lambda_seq_ = 0;
  spawn_seq_ = 0;
  delay_seq_ = 0;
  global_init_fn_ = nullptr;
  global_deinit_fn_ = nullptr;
  inject_deinit_on_return_ = false;

  ensureDataLayout();

  // `use` aliases are pure compile-time renames: resolve them before any symbol
  // lookup so `Server::start()` and `Server(...)` reach their namespaced targets.
  for (const ast::UseAlias& u : module.usages) {
    if (!u.alias.empty() && !u.scope.empty()) {
      use_aliases_[u.alias] = u.scope;
    }
  }

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    // [A007] position context for helpers that only see erased AST nodes
    // (pattern-matching compiler-generated `clone` / `to_string` bodies).
    current_top_level_loc_ = stmt->loc;
    if (stmt->kind() != ast::StmtKind::InterfaceDecl) {
      continue;
    }
    if (!registerInterface(static_cast<const ast::InterfaceDeclStmt&>(*stmt))) {
      return false;
    }
  }

  // extern module FIRST: class structs may embed generic instantiations
  // (`Map<Variant>` field), and their specialized bodies call l_alloc / l_realloc.
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    current_top_level_loc_ = stmt->loc;
    if (stmt->kind() != ast::StmtKind::ExternModule) {
      continue;
    }
    const auto& ext = static_cast<const ast::ExternModuleStmt&>(*stmt);
    for (const auto& decl : ext.decls) {
      if (!decl) {
        continue;
      }
      if (!declareExternFunc(*decl)) {
        return false;
      }
    }
  }

  // Register generic templates before anything specializes them.
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    current_top_level_loc_ = stmt->loc;
    if (stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    const auto& cls = static_cast<const ast::ClassDeclStmt&>(*stmt);
    if (cls.generics.empty()) {
      continue;
    }
    if (generic_class_templates_.count(cls.name) != 0) {
      error(cls.loc, "redefinition of generic class: " + cls.name);
      return false;
    }
    generic_class_templates_[cls.name] = &cls;
  }

  // `String` is a dependency of nearly every stdlib module, so its struct +
  // methods go first. Only then can generic instantiations that use it
  // (`Map<Variant>` → `keys()`) emit method bodies cleanly.
  in_struct_pass_ = true;
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    const auto& cls = static_cast<const ast::ClassDeclStmt&>(*stmt);
    if (cls.name != "String" || !cls.generics.empty()) {
      continue;
    }
    if (!emitClassStruct(cls) || !declareClassMethods(cls) || !buildVtables(cls)) {
      return false;
    }
  }

  // TEMP_HACK_GENFIRST
  resettlePendingStructBodies();
  if (!drainPendingGenericBodies()) {
    return false;
  }

  // Layout every non-generic class. A class whose field types a still-opaque
  // struct is postponed (`deferred_bodies_`) and retried after each successful
  // layout, so declaration order does not matter (a `-> String` signature may
  // force `String` to register before this pass runs).
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    const auto& cls = static_cast<const ast::ClassDeclStmt&>(*stmt);
    if (!cls.generics.empty() || cls.name == "String") {
      continue;  // template registered above; String laid out in the first pass
    }
    if (!emitClassStruct(cls)) {
      return false;
    }
  }
  // TEMP_HACK_ORDER
  // A postponed class is always retried after its dependency got a body; a cycle
  // of opaque dependencies can never settle, so bound the retries.
  for (std::size_t guard = 0; !deferred_bodies_.empty(); ++guard) {
    if (guard >= deferred_bodies_.size()) {
      error(lastTopLevelLoc(), "cyclic class layout involving '" + deferred_bodies_.front()->name + "'");
      // [A007] no AST loc available
      return false;
    }
    const ast::ClassDeclStmt* decl = deferred_bodies_.front();
    deferred_bodies_.erase(deferred_bodies_.begin());
    if (classes_.count(decl->name) != 0) {
      continue;  // laid out meanwhile
    }
    if (!emitClassStruct(*decl)) {
      return false;
    }
  }

  // Every non-generic body is sized now, so deferred specializations are safe.
  in_struct_pass_ = false;
  if (!drainPendingGenericBodies()) {
    return false;
  }

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    const auto& cls = static_cast<const ast::ClassDeclStmt&>(*stmt);
    if (cls.name == "String" && cls.generics.empty()) {
      continue;  // already declared above
    }
    if (!declareClassMethods(cls) || !buildVtables(cls)) {
      return false;
    }
  }

  // Generic functions are templates — store, do not emit IR yet.
  // Concrete functions get declarations now (private names are module-mangled).
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::FuncDecl) {
      continue;
    }
    const auto& fn = static_cast<const ast::FuncDeclStmt&>(*stmt);
    if (fn.is_method) {
      continue;
    }
    // Package `.li` / signature-only pubs: C ABI external (exact symbol name).
    if (fn.is_extern) {
      if (!declareExternFunc(fn)) {
        return false;
      }
      continue;
    }
    // The String struct is laid out before function declarations so `-> String`
    // resolves for every forward declaration below.
    // Compile-time only: executed by the LCMD interpreter, never lowered to LLVM.
    if (fn.name == "__compile_time_handler" || fn.name == "__on_class_parsed") {
      continue;
    }
    const std::string ir_name = mangleName(fn.module_name, fn.name, fn.is_pub);
    if (fn.is_pub || fn.name == "main") {
      public_funcs_[fn.name] = ir_name;
      // Cross-module: only `pub` functions enter a module's export table, so
      // `mod::private_fn()` cannot be resolved (the semantic pass rejects it).
      if (fn.is_pub && !fn.module_name.empty()) {
        module_exports_[fn.module_name][fn.name] = &fn;
      }
    } else {
      private_funcs_[fn.module_name + "::" + fn.name] = ir_name;
    }
    if (!fn.generics.empty()) {
      const std::string tmpl_key =
          fn.is_pub ? fn.name : (fn.module_name + "::" + fn.name);
      if (generic_templates_.count(tmpl_key) != 0) {
        error(fn.loc, "redefinition of generic function: " + fn.name);
        return false;
      }
      generic_templates_[tmpl_key] = &fn;
      continue;
    }
    // Concrete funcs: keep AST for keyword-argument reordering at call sites.
    if (fn.is_pub || fn.name == "main") {
      concrete_funcs_[fn.name] = &fn;
    } else {
      concrete_funcs_[fn.module_name + "::" + fn.name] = &fn;
      concrete_funcs_[fn.name] = &fn;  // same-module short name
    }
    std::vector<llvm::Type*> params;
    // CRT entry: always `int main(int argc, char** argv)` so argv is capturable.
    if (fn.name == "main") {
      params.push_back(i32Ty());
      params.push_back(ptrTy());
    } else {
      for (const ast::Param& p : fn.params) {
        params.push_back(abiTypeForParam(p.type_name));
      }
    }
    llvm::Type* ret_ty = nullptr;
    if (!fn.returns_fat_closure && !fn.return_type.empty() && fn.return_type != "void") {
      if (!ensureTypeAvailable(fn.return_type)) {
        deferred_return_types_.push_back(&fn);
      }
      ret_ty = fn.returns_fat_closure ? static_cast<llvm::Type*>(fatClosureTy())
                                      : typeFromName(fn.return_type);
    } else {
      ret_ty = fn.returns_fat_closure ? static_cast<llvm::Type*>(fatClosureTy())
                                      : typeFromName(fn.return_type);
    }
    if (fn.is_async) {
      ret_ty = ptrTy();
    }
    auto* fty = llvm::FunctionType::get(ret_ty, params, false);
    const auto linkage = (fn.is_pub || fn.name == "main") ? llvm::Function::ExternalLinkage
                                                          : llvm::Function::InternalLinkage;
    llvm::Function* lf = llvm::Function::Create(fty, linkage, ir_name, module_.get());
    // C ABI so L functions can be passed to extern C as callbacks safely.
    lf->setCallingConv(llvm::CallingConv::C);
    // Shared library: export every `pub` entry so foreign loaders (ctypes / Fiddle) see it.
    if (shared_mode_ && fn.is_pub && lf) {
      lf->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
      lf->setVisibility(llvm::GlobalValue::DefaultVisibility);
    }
  }

  fatPtrTy();

  // The String struct must be laid out before it is referenced: registrations
  // above (`Config` nested field, `-> String` signatures) may only see an opaque
  // placeholder when the stdlib was loaded before this module.
  // Only fail when the module actually declares `String` yet it could not be
  // laid out (e.g. a field of an unresolvable type). A module that never loads
  // stdlib/string.l simply has no String type; its literals are lowered as `ptr`.
  if (classes_.count("String") != 0 && !ensureTypeAvailable("String")) {
    error(lastTopLevelLoc(), "String type is not available");
    // [A007] no AST loc available
    return false;
  }

  // Module-level variables before any function body (GlobalVariable / const inline).
  //
  // `emitGlobalVarDecl` is only reached for the primary module's statements, so
  // an imported module's `const` (e.g. `KIND_POS` in apps/cad/entity.l) would
  // never be inlined and used to fail as an "undeclared identifier" from the
  // importing module's method bodies. Register every `const` up front.
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::VarDecl) {
      continue;
    }
    const auto& decl = static_cast<const ast::VarDeclStmt&>(*stmt);
    if (decl.decl_kind == ast::VarDeclKind::Const) {
      registerModuleConst(decl);
    }
  }
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::VarDecl) {
      continue;
    }
    const auto& decl = static_cast<const ast::VarDeclStmt&>(*stmt);
    if (decl.decl_kind == ast::VarDeclKind::Const) {
      continue;  // inlined above, no global emitted
    }
    if (!emitGlobalVarDecl(decl)) {
      return false;
    }
  }

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    if (!emitClassMethodBodies(static_cast<const ast::ClassDeclStmt&>(*stmt))) {
      return false;
    }
  }

  // Return types that were not materialized when the signatures were declared
  // (`String`, another module's class) are Class* already; just register the
  // struct now that every class body is sized.
  // Aliased declarations (http_client_get) are lowered as part of their source
  // module, where `String` is laid out; skip them here.
  if (isClassType("String")) {
    for (const ast::FuncDeclStmt* fn : deferred_return_types_) {
      if (!ensureTypeAvailable(fn->return_type)) {
        error(fn->loc, "cannot resolve return type '" + fn->return_type + "' of " + fn->name);
        return false;
      }
    }
  }
  deferred_return_types_.clear();
  if (in_error_) {
    return false;
  }

  // Build __l_global_init / __l_global_deinit before main so calls resolve.
  if (!emitLifecycleWrappers(module)) {
    return false;
  }

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    if (stmt->kind() == ast::StmtKind::InterfaceDecl ||
        stmt->kind() == ast::StmtKind::ClassDecl ||
        stmt->kind() == ast::StmtKind::VarDecl ||
        isModuleTopLevelExecStmt(*stmt)) {
      // Top-level Block/Assign/ExprStmt already ran inside __l_global_init.
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
    if (in_error_) {
      return false;  // first error already reported; keep the IR un-emitted
    }
  }

  if (llvm::verifyModule(*module_, &llvm::errs())) {
    error(lastTopLevelLoc(), "module verification failed");
    // [A007] no AST loc available
    return false;
  }
  return !had_error_;
}

void Codegen::optimizeModule(llvm::TargetMachine* tm) {
  if (opt_level_ == 0 || !tm) {
    return;
  }

  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;

  llvm::PassBuilder pb(tm);
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::OptimizationLevel level = llvm::OptimizationLevel::O0;
  if (opt_level_ == 1) {
    level = llvm::OptimizationLevel::O1;
  } else if (opt_level_ == 2) {
    level = llvm::OptimizationLevel::O2;
  } else {
    level = llvm::OptimizationLevel::O3;
  }

  llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
  mpm.run(*module_, mam);
}

bool Codegen::emitObjectFile(const std::string& object_path) {
  ensureNativeTargets();

  const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  module_->setTargetTriple(triple);

  std::string err;
  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
  if (!target) {
    error(lastTopLevelLoc(), "lookupTarget failed: " + err);
    // [A007] no AST loc available
    return false;
  }

  llvm::TargetOptions opt;
  // Shared objects need PIC so the dynamic loader can relocate them.
  auto reloc = shared_mode_ ? std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_)
                            : std::optional<llvm::Reloc::Model>();
  const std::string cpu =
      opt_level_ > 0 ? std::string(llvm::sys::getHostCPUName()) : std::string("generic");
  const std::string cpu_name = cpu.empty() ? "generic" : cpu;
  std::unique_ptr<llvm::TargetMachine> tm(
      target->createTargetMachine(triple, cpu_name, "", opt, reloc));
  if (!tm) {
    error(lastTopLevelLoc(), "failed to create TargetMachine");
    // [A007] no AST loc available
    return false;
  }

  module_->setDataLayout(tm->createDataLayout());
  // `-g`: finalize the compile unit before optimization so the pass pipeline
  // preserves line tables and the object gets a .debug_info / .pdb record.
  finishDebugInfo();
  optimizeModule(tm.get());

  std::error_code ec;
  llvm::raw_fd_ostream dest(object_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    error(lastTopLevelLoc(), "could not open object file '" + object_path + "': " + ec.message());
    // [A007] no AST loc available
    return false;
  }

  llvm::legacy::PassManager pass;
  if (tm->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    error(lastTopLevelLoc(), "TargetMachine cannot emit object file");
    // [A007] no AST loc available
    return false;
  }
  pass.run(*module_);
  dest.flush();
  return true;
}

bool Codegen::emitStmt(const ast::Stmt& stmt) {
  // `-g`: line tables are driven from statement locations (Expr nodes inherit
  // the enclosing statement's line in the emitted instructions).
  if (di_builder_) {
    setDebugLoc(stmt.loc);
  }
  switch (stmt.kind()) {
    case ast::StmtKind::InterfaceDecl:
    case ast::StmtKind::ClassDecl:
      return true;
    case ast::StmtKind::FieldDecl:
      error(stmt.loc, "field declaration outside class");
      return false;
    case ast::StmtKind::FuncDecl:
      return emitFuncDecl(static_cast<const ast::FuncDeclStmt&>(stmt));
    case ast::StmtKind::VarDecl:
      return emitVarDecl(static_cast<const ast::VarDeclStmt&>(stmt));
    case ast::StmtKind::DestructureDecl:
      return emitDestructureDecl(static_cast<const ast::DestructureDeclStmt&>(stmt));
    case ast::StmtKind::Assign:
      return emitAssign(static_cast<const ast::AssignStmt&>(stmt));
    case ast::StmtKind::Return:
      return emitReturn(static_cast<const ast::ReturnStmt&>(stmt));
    case ast::StmtKind::ExprStmt:
      return emitExprStmt(static_cast<const ast::ExprStmt&>(stmt));
    case ast::StmtKind::Block: {
      const auto& blk = static_cast<const ast::BlockStmt&>(stmt);
      // [dsl] `server { ... }` — rebind `this` to the section receiver.
      if (!blk.section_field.empty() && current_this_ && !current_class_.empty()) {
        const ClassInfo* owner = findClass(current_class_);
        if (!owner) {
          error(stmt.loc, "unknown config class '" + current_class_ + "'");
          return false;
        }
        llvm::Value* section = nullptr;
        std::string section_ty;
        if (current_class_ == "CPropertys") {
          section = emitConfigSectionPtr(blk.section_field, current_class_, blk.loc);
          section_ty = "CPropertys";
        } else {
          const std::string phys = physicalFieldName(blk.section_field);
          auto fit = owner->field_index.find(phys);
          if (fit == owner->field_index.end()) {
            error(stmt.loc, "unknown section '" + blk.section_field + "' on '" +
                               current_class_ + "'");
            return false;
          }
          if (fit->second < owner->field_types.size()) {
            section_ty = resolveTypeName(owner->field_types[fit->second]);
          }
          section = builder_.CreateStructGEP(owner->type, current_this_, fit->second,
                                             blk.section_field + ".section");
        }
        if (!section) {
          return false;
        }
        const std::string saved_class = current_class_;
        llvm::Value* saved_this = current_this_;
        current_class_ = section_ty;
        current_this_ = section;
        bool ok = emitStmtListScoped(blk.stmts);
        current_class_ = saved_class;
        current_this_ = saved_this;
        return ok;
      }
      return emitStmtListScoped(blk.stmts);
    }
    case ast::StmtKind::If:
      return emitIf(static_cast<const ast::IfStmt&>(stmt));
    case ast::StmtKind::Match:
      return emitMatch(static_cast<const ast::MatchStmt&>(stmt));
    case ast::StmtKind::While:
      return emitWhile(static_cast<const ast::WhileStmt&>(stmt));
    case ast::StmtKind::Break:
      return emitBreak(static_cast<const ast::BreakStmt&>(stmt));
    case ast::StmtKind::Continue:
      return emitContinue(static_cast<const ast::ContinueStmt&>(stmt));
    case ast::StmtKind::ForIn:
      return emitForIn(static_cast<const ast::ForInStmt&>(stmt));
    case ast::StmtKind::Defer:
      return emitDefer(static_cast<const ast::DeferStmt&>(stmt));
    case ast::StmtKind::Sync:
      return emitSync(static_cast<const ast::SyncBlockStmt&>(stmt));
    case ast::StmtKind::Spawn:
      return emitSpawn(static_cast<const ast::SpawnBlockStmt&>(stmt));
    case ast::StmtKind::Import:
      return true;  // imports are expanded by the module loader
    case ast::StmtKind::ExternModule:
      return true;  // declarations emitted in emit() preamble
    case ast::StmtKind::Directive:
      error(stmt.loc, "unexpanded LCMD directive (frontend expand missing?)");
      return false;
    case ast::StmtKind::Comptime:
      error(stmt.loc, "unexpanded comptime block (frontend expand missing?)");
      return false;
  }
  // [A007] no AST loc available — `emitStmt` only sees the erased `ast::Stmt` base.
  error(stmt.loc, "unsupported statement in codegen");
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
    params.push_back(abiTypeForParam(resolved));
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
  auto saved_owned_strings = owned_string_locals_;
  auto saved_readonly = readonly_refs_;
  auto saved_defers = defer_scopes_;
  auto saved_autofree = auto_free_scopes_;
  auto saved_fn_ret = current_fn_return_type_;
  auto saved_insert = builder_.GetInsertBlock();
  auto saved_point = builder_.GetInsertPoint();

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  named_values_.clear();
  local_types_.clear();
  heap_locals_.clear();
  owned_string_locals_.clear();
  arena_locals_.clear();
  readonly_refs_.clear();
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  current_fn_return_type_ = resolved_ret;
  enterDeferScope();

  unsigned idx = 0;
  for (auto& arg : fn->args()) {
    const ast::Param& param = tmpl.params[idx];
    const std::string resolved = resolved_param_types[idx];
    arg.setName(param.name);
    bindParam(fn, &arg, param, resolved);
    ++idx;
  }

  bool ok = emitStmtList(tmpl.body);

  if (ok && builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    ok = finishCleanups();
    if (ok) {
      if (fn->getReturnType()->isVoidTy()) {
        builder_.CreateRetVoid();
      } else if (fn->getReturnType()->isIntegerTy(32)) {
        builder_.CreateRet(llvm::ConstantInt::get(i32Ty(), 0));
      } else {
        error(tmpl.loc, "missing return in specialized function: " + mangled);
        ok = false;
      }
    }
  }

  if (ok && llvm::verifyFunction(*fn, &llvm::errs())) {
    error(tmpl.loc, "specialized function verification failed: " + mangled);
    ok = false;
  }

  type_subst_.clear();
  named_values_ = std::move(saved_named);
  local_types_ = std::move(saved_locals);
  heap_locals_ = std::move(saved_heap);
  owned_string_locals_ = std::move(saved_owned_strings);
  readonly_refs_ = std::move(saved_readonly);
  defer_scopes_ = std::move(saved_defers);
  auto_free_scopes_ = std::move(saved_autofree);
  current_fn_return_type_ = std::move(saved_fn_ret);
  if (saved_insert) {
    builder_.SetInsertPoint(saved_insert, saved_point);
  }
  return ok;
}

llvm::Function* Codegen::ensureSpecialized(const ast::FuncDeclStmt& tmpl,
                                           const std::vector<std::string>& arg_type_names,
                                           const ast::SourceLocation& call_loc) {
  if (tmpl.params.size() != arg_type_names.size()) {
    error(call_loc, "argument count mismatch for generic call to " + tmpl.name);
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
      error(call_loc, "cannot infer generic parameter " + gp.name + " for " + tmpl.name);
      return nullptr;
    }
    const std::string& concrete = subst[gp.name];
    if (!ensureTypeAvailable(concrete) || !isClassType(concrete)) {
      error(call_loc, "generic " + gp.name + " inferred non-class type '" + concrete + "'");
      return nullptr;
    }
    if (!gp.constraint.empty() && !classImplements(concrete, gp.constraint)) {
      error(call_loc, "Type '" + concrete + "' does not implement interface '" + gp.constraint +
                          "'");
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

bool Codegen::emitHookBodies(llvm::Function* fn,
                             const std::vector<std::unique_ptr<ast::FuncDeclStmt>>& hooks) {
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  named_values_.clear();
  local_types_.clear();
  closure_locals_.clear();
  heap_locals_.clear();
  owned_string_locals_.clear();
  arena_locals_.clear();
  readonly_refs_.clear();
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  held_sync_locks_.clear();
  current_fn_return_type_ = "void";
  enterDeferScope();

  for (const auto& hook : hooks) {
    if (!hook) {
      continue;
    }
    current_top_level_loc_ = hook->loc;
    current_module_ = hook->module_name;
    if (!emitStmtList(hook->body)) {
      current_fn_return_type_.clear();
      return false;
    }
    if (builder_.GetInsertBlock() && builder_.GetInsertBlock()->getTerminator()) {
      break;
    }
  }

  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    if (!finishCleanups()) {
      current_fn_return_type_.clear();
      return false;
    }
    builder_.CreateRetVoid();
  }
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  held_sync_locks_.clear();
  current_fn_return_type_.clear();

  if (llvm::verifyFunction(*fn, &llvm::errs())) {
    error(lastTopLevelLoc(), "lifecycle hook verification failed: " + fn->getName().str());
    // [A007] no AST loc available
    fn->eraseFromParent();
    return false;
  }
  return true;
}

bool Codegen::emitPendingGlobalClassInits() {
  for (const PendingGlobalClassInit& p : pending_global_class_inits_) {
    if (!p.neu) {
      // [A007] no AST loc available — PendingGlobalClassInit carries no location.
      error(lastTopLevelLoc(), "global class '" + p.name + "' missing NewExpr initializer");
      return false;
    }
    if (!ensureTypeAvailable(p.class_name)) {
      error(p.neu->loc, "unknown class for global '" + p.name + "': " + p.class_name);
      return false;
    }
    const ClassInfo* info = findClass(p.class_name);
    auto git = global_vars_.find(p.name);
    if (!info || git == global_vars_.end()) {
      error(p.neu->loc, "global class '" + p.name + "' not registered");
      return false;
    }
    if (!initClassAtPtr(git->second, *info, *p.neu)) {
      return false;
    }
  }
  return true;
}

bool Codegen::emitLifecycleWrappers(const ast::Module& module) {
  global_init_fn_ = nullptr;
  global_deinit_fn_ = nullptr;

  bool has_toplevel_exec = false;
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (stmt && isModuleTopLevelExecStmt(*stmt)) {
      has_toplevel_exec = true;
      break;
    }
  }

  const bool need_global_init = !module.init_hooks.empty() ||
                                !pending_global_class_inits_.empty() || has_toplevel_exec;
  // [A007] `emitPendingGlobalClassInits` has no AST node of its own.
  current_top_level_loc_ = module.stmts.empty() || !module.stmts.front()
                               ? ast::SourceLocation{}
                               : module.stmts.front()->loc;
  if (need_global_init) {
    auto* fty = llvm::FunctionType::get(voidTy(), {}, false);
    global_init_fn_ = llvm::Function::Create(fty, llvm::Function::InternalLinkage,
                                             "__l_global_init", module_.get());
    auto saved_insert = builder_.GetInsertBlock();
    auto saved_point = builder_.GetInsertPoint();

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", global_init_fn_);
    builder_.SetInsertPoint(entry);
    named_values_.clear();
    local_types_.clear();
    closure_locals_.clear();
    heap_locals_.clear();
    owned_string_locals_.clear();
  owned_string_locals_.clear();
    arena_locals_.clear();
    readonly_refs_.clear();
    defer_scopes_.clear();
    auto_free_scopes_.clear();
    held_sync_locks_.clear();
    current_fn_return_type_ = "void";
    enterDeferScope();

    // Class globals first, then top-level config stmts (app_cfg.l), then __init hooks.
    if (!emitPendingGlobalClassInits()) {
      return false;
    }
    for (const ast::StmtPtr& stmt : module.stmts) {
      if (!stmt) {
        continue;
      }
      current_top_level_loc_ = stmt->loc;
      if (!isModuleTopLevelExecStmt(*stmt)) {
        continue;
      }
      if (!emitStmt(*stmt)) {
        current_fn_return_type_.clear();
        return false;
      }
      if (builder_.GetInsertBlock() && builder_.GetInsertBlock()->getTerminator()) {
        break;
      }
    }
    for (const auto& hook : module.init_hooks) {
      if (!hook) {
        continue;
      }
      current_module_ = hook->module_name;
      if (!emitStmtList(hook->body)) {
        current_fn_return_type_.clear();
        return false;
      }
      if (builder_.GetInsertBlock() && builder_.GetInsertBlock()->getTerminator()) {
        break;
      }
    }
    if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
      if (!finishCleanups()) {
        current_fn_return_type_.clear();
        return false;
      }
      builder_.CreateRetVoid();
    }
    defer_scopes_.clear();
    auto_free_scopes_.clear();
    held_sync_locks_.clear();
    current_fn_return_type_.clear();

    if (llvm::verifyFunction(*global_init_fn_, &llvm::errs())) {
      error(lastTopLevelLoc(), "lifecycle hook verification failed: __l_global_init");
      // [A007] no AST loc available
      global_init_fn_->eraseFromParent();
      global_init_fn_ = nullptr;
      return false;
    }

    if (saved_insert) {
      builder_.SetInsertPoint(saved_insert, saved_point);
    }
  }

  if (!module.deinit_hooks.empty()) {
    auto* fty = llvm::FunctionType::get(voidTy(), {}, false);
    global_deinit_fn_ = llvm::Function::Create(fty, llvm::Function::InternalLinkage,
                                               "__l_global_deinit", module_.get());
    auto saved_insert = builder_.GetInsertBlock();
    auto saved_point = builder_.GetInsertPoint();
    if (!emitHookBodies(global_deinit_fn_, module.deinit_hooks)) {
      return false;
    }
    if (saved_insert) {
      builder_.SetInsertPoint(saved_insert, saved_point);
    }
  }
  return true;
}

void Codegen::emitLifecycleDeinitCall() {
  if (inject_deinit_on_return_ && global_deinit_fn_) {
    builder_.CreateCall(global_deinit_fn_);
  }
}

bool Codegen::emitAsyncFuncDecl(const ast::FuncDeclStmt& fn_ast) {
  return emitCoroutineBody(fn_ast);
}

namespace {

// Every local that needs a frame slot: `VarDecl` names reachable from the body.
// The MVP frame is laid out from this static scan, so a local survives the
// suspend point no matter where in the body it was declared.
void collectCoroLocals(const std::vector<ast::StmtPtr>& stmts,
                       std::vector<const ast::VarDeclStmt*>& out) {
  for (const ast::StmtPtr& st : stmts) {
    if (!st) {
      continue;
    }
    switch (st->kind()) {
      case ast::StmtKind::VarDecl:
        out.push_back(&static_cast<const ast::VarDeclStmt&>(*st));
        break;
      case ast::StmtKind::If: {
        const auto& iff = static_cast<const ast::IfStmt&>(*st);
        collectCoroLocals(iff.then_body, out);
        collectCoroLocals(iff.else_body, out);
        break;
      }
      case ast::StmtKind::Block:
        collectCoroLocals(static_cast<const ast::BlockStmt&>(*st).stmts, out);
        break;
      default:
        break;  // `await` in loops is rejected before we get here
    }
  }
}

int countAwaits(const std::vector<ast::StmtPtr>& stmts) {
  int n = 0;
  for (const ast::StmtPtr& st : stmts) {
    if (!st) {
      continue;
    }
    switch (st->kind()) {
      case ast::StmtKind::ExprStmt: {
        const auto& es = static_cast<const ast::ExprStmt&>(*st);
        if (es.expr && es.expr->kind() == ast::ExprKind::Await) {
          ++n;
        }
        break;
      }
      case ast::StmtKind::Return: {
        const auto& rs = static_cast<const ast::ReturnStmt&>(*st);
        if (rs.value && rs.value->kind() == ast::ExprKind::Await) {
          ++n;
        }
        break;
      }
      case ast::StmtKind::VarDecl: {
        // `x := await f()` — the common shape of an awaited call.
        const auto& vd = static_cast<const ast::VarDeclStmt&>(*st);
        if (vd.init && vd.init->kind() == ast::ExprKind::Await) {
          ++n;
        }
        break;
      }
      case ast::StmtKind::If: {
        const auto& iff = static_cast<const ast::IfStmt&>(*st);
        n += countAwaits(iff.then_body);
        n += countAwaits(iff.else_body);
        break;
      }
      case ast::StmtKind::Block:
        n += countAwaits(static_cast<const ast::BlockStmt&>(*st).stmts);
        break;
      default:
        break;
    }
  }
  return n;
}

// `await` nested in a loop has no single resume state in the MVP.
bool hasAwaitInLoop(const std::vector<ast::StmtPtr>& stmts) {
  for (const ast::StmtPtr& st : stmts) {
    if (!st) {
      continue;
    }
    switch (st->kind()) {
      case ast::StmtKind::While: {
        const auto& wh = static_cast<const ast::WhileStmt&>(*st);
        if (countAwaits(wh.body) > 0) {
          return true;
        }
        if (hasAwaitInLoop(wh.body)) {
          return true;
        }
        break;
      }
      case ast::StmtKind::ForIn: {
        const auto& fi = static_cast<const ast::ForInStmt&>(*st);
        if (countAwaits(fi.body) > 0) {
          return true;
        }
        if (hasAwaitInLoop(fi.body)) {
          return true;
        }
        break;
      }
      case ast::StmtKind::If: {
        const auto& iff = static_cast<const ast::IfStmt&>(*st);
        if (hasAwaitInLoop(iff.then_body) || hasAwaitInLoop(iff.else_body)) {
          return true;
        }
        break;
      }
      case ast::StmtKind::Block:
        if (hasAwaitInLoop(static_cast<const ast::BlockStmt&>(*st).stmts)) {
          return true;
        }
        break;
      default:
        break;
    }
  }
  return false;
}

}  // namespace

// Lower `async fun f(...) -> T` into a hand-written stackless state machine.
//
// LLVM's CoroSplit pass silently refuses to split our IR (it leaves `coro.id`
// and `coro.suspend` in place but never outlines `.resume`/`.destroy`), so the
// frame, the state index and the suspend/resume jumps are emitted explicitly:
//
//   f(params) -> ptr              ; entry wrapper: heap frame + task, then step
//   f$step(ptr frame) -> void     ; the state machine itself
//
//   f$step dispatch:  switch frame.state { 0 -> body }
//   body:             <statements before `await`>
//                     frame.state = 1
//                     frame.await = <value>
//                     ret            ; suspend: control returns to the caller
//   await.resume:     <statements after `await`>
//   done:             publish frame.result to the task, mark it complete, ret
//
// Every coroutine local (and every parameter) lives in the frame, so nothing
// is lost across the suspend point. MVP: exactly one `await`, never in a loop.
bool Codegen::emitCoroutineBody(const ast::FuncDeclStmt& fn_ast) {
  const std::string ir_name = mangleName(fn_ast.module_name, fn_ast.name, fn_ast.is_pub);
  llvm::Function* fn = module_->getFunction(ir_name);
  if (!fn) {
    error(fn_ast.loc, "missing function declaration: " + ir_name);
    return false;
  }
  if (!fn->empty()) {
    error(fn_ast.loc, "redefinition of function: " + ir_name);
    return false;
  }

  // ---- MVP restrictions ----
  if (hasAwaitInLoop(fn_ast.body)) {
    error(fn_ast.loc, "'await' inside a loop is not supported yet (async MVP)");
    return false;
  }
  if (countAwaits(fn_ast.body) > 1) {
    error(fn_ast.loc, "'async fun " + fn_ast.name +
                          "': only one 'await' per async function is supported (async MVP)");
    return false;
  }

  llvm::Type* i8p = ptrTy();
  llvm::Type* i32 = i32Ty();
  llvm::Type* i64 = llvm::Type::getInt64Ty(globalContext());
  llvm::Type* void_ty = llvm::Type::getVoidTy(globalContext());
  llvm::PointerType* i8p_ty = llvm::dyn_cast<llvm::PointerType>(i8p);

  // ---- Frame layout: { i32 state, ptr task, T result, <params + locals> } ----
  std::string ret_name = fn_ast.return_type.empty() ? "i32" : fn_ast.return_type;
  ret_name = resolveTypeName(ret_name);
  llvm::Type* ret_ty = typeFromName(ret_name);
  if (!ret_ty || ret_ty->isVoidTy()) {
    ret_ty = i32;
  }
  constexpr unsigned kStateIdx = 0;
  constexpr unsigned kTaskIdx = 1;
  constexpr unsigned kResultIdx = 2;

  std::vector<llvm::Type*> fields = {i32, i8p, ret_ty};
  coro_local_slots_.clear();
  for (const ast::Param& param : fn_ast.params) {
    std::string tn = resolveTypeName(param.type_name);
    llvm::Type* pt = (isClassType(tn) || isFunTypeName(tn)) ? ptrTy() : typeFromName(tn);
    if (!pt) {
      pt = i32;
    }
    coro_local_slots_[param.name] = static_cast<unsigned>(fields.size());
    fields.push_back(pt);
  }
  std::vector<const ast::VarDeclStmt*> locals;
  collectCoroLocals(fn_ast.body, locals);
  for (const ast::VarDeclStmt* vd : locals) {
    if (!vd || vd->decl_kind == ast::VarDeclKind::Const ||
        vd->decl_kind == ast::VarDeclKind::Constd) {
      continue;  // compile-time constants never need a frame slot
    }
    std::string tn = vd->type_name;
    if (tn.empty() && vd->init) {
      tn = inferTypeFromExpr(*vd->init);
    }
    tn = resolveTypeName(tn.empty() ? "i32" : tn);
    llvm::Type* lt = (isClassType(tn) || isFunTypeName(tn)) ? ptrTy() : typeFromName(tn);
    if (!lt) {
      lt = i32;
    }
    coro_local_slots_[vd->name] = static_cast<unsigned>(fields.size());
    fields.push_back(lt);
  }

  coro_frame_ty_ = llvm::StructType::create(globalContext(), fields, ir_name + ".frame");
  coro_result_idx_ = kResultIdx;
  // Slot reserved for the awaited task handle (filled in by `await`).
  coro_await_task_idx_ = static_cast<unsigned>(coro_frame_ty_->getNumElements());
  {
    std::vector<llvm::Type*> with_await = fields;
    with_await.push_back(i8p);
    coro_frame_ty_->setBody(with_await, /*isPacked=*/false);
  }

  // ---- Runtime hooks (runtime/task.c) ----
  llvm::FunctionCallee alloc_fn =
      module_->getOrInsertFunction("l_alloc", llvm::FunctionType::get(i8p, {i64}, false));
  llvm::FunctionCallee task_new_fn =
      module_->getOrInsertFunction("l_task_new", llvm::FunctionType::get(i8p, {i8p, i8p}, false));
  llvm::FunctionCallee set_frame_fn = module_->getOrInsertFunction(
      "l_task_set_frame", llvm::FunctionType::get(void_ty, {i8p, i8p}, false));
  llvm::FunctionCallee set_resume_fn = module_->getOrInsertFunction(
      "l_task_set_resume", llvm::FunctionType::get(void_ty, {i8p, i8p}, false));
  llvm::FunctionCallee set_result_fn = module_->getOrInsertFunction(
      "l_task_set_result", llvm::FunctionType::get(void_ty, {i8p, i8p}, false));
  llvm::FunctionCallee complete_fn = module_->getOrInsertFunction(
      "l_task_complete", llvm::FunctionType::get(void_ty, {i8p}, false));
  llvm::FunctionCallee enqueue_fn =
      module_->getOrInsertFunction("l_task_enqueue", llvm::FunctionType::get(void_ty, {i8p}, false));

  llvm::Constant* null_ptr = llvm::ConstantPointerNull::get(i8p_ty);
  llvm::Function* step_fn = llvm::Function::Create(
      llvm::FunctionType::get(void_ty, {i8p}, false), llvm::Function::InternalLinkage,
      ir_name + "$step", module_.get());
  step_fn->setCallingConv(llvm::CallingConv::C);

  // ---- Entry wrapper: f(params) -> ptr ----
  {
    llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(globalContext(), "entry", fn);
    llvm::IRBuilder<> eb(entry_bb);
    llvm::Value* frame_mem =
        eb.CreateCall(alloc_fn, {llvm::ConstantExpr::getSizeOf(coro_frame_ty_)}, "frame.mem");
    llvm::Value* frame = eb.CreateBitCast(frame_mem, ptrTy(), "frame");
    eb.CreateStore(llvm::ConstantInt::get(i32, 0),
                   eb.CreateStructGEP(coro_frame_ty_, frame, kStateIdx, "state.ptr"));
    unsigned arg_idx = 0;
    for (auto& arg : fn->args()) {
      const ast::Param& param = fn_ast.params[arg_idx++];
      eb.CreateStore(&arg, eb.CreateStructGEP(coro_frame_ty_, frame,
                                              coro_local_slots_[param.name],
                                              param.name + ".slot"));
    }
    llvm::Value* task = eb.CreateCall(task_new_fn, {null_ptr, frame}, "task");
    eb.CreateStore(task, eb.CreateStructGEP(coro_frame_ty_, frame, kTaskIdx, "task.slot"));
    eb.CreateCall(set_frame_fn, {task, frame});
    eb.CreateCall(set_resume_fn, {task, step_fn});
    // Queue the first step; `l_executor_run_all()` / `l_task_await()` drives it.
    eb.CreateCall(enqueue_fn, {task});
    eb.CreateRet(task);
  }

  // ---- State machine: f$step(ptr frame) -> void ----
  llvm::BasicBlock* dispatch_bb = llvm::BasicBlock::Create(globalContext(), "dispatch", step_fn);
  llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(globalContext(), "body", step_fn);
  llvm::BasicBlock* resume_bb = llvm::BasicBlock::Create(globalContext(), "await.resume", step_fn);
  llvm::BasicBlock* done_bb = llvm::BasicBlock::Create(globalContext(), "done", step_fn);

  builder_.SetInsertPoint(dispatch_bb);
  llvm::Value* frame = step_fn->arg_begin();
  frame->setName("frame");
  llvm::Value* state_ptr = builder_.CreateStructGEP(coro_frame_ty_, frame, kStateIdx, "state.ptr");
  llvm::Value* st = builder_.CreateLoad(i32, state_ptr, "state");
  // Cache every local's slot address in the dispatch block: it dominates all
  // subsequent blocks (body, await continuation, done), so a slot created once
  // here stays usable after the suspend/resume boundary.
  coro_slot_addrs_.clear();
  for (const auto& [name, idx] : coro_local_slots_) {
    coro_slot_addrs_[name] =
        builder_.CreateStructGEP(coro_frame_ty_, frame, idx, name + ".addr");
  }
  llvm::SwitchInst* sw = builder_.CreateSwitch(st, body_bb, 1);
  coro_dispatch_sw_ = sw;

  coro_frame_ = frame;
  coro_done_bb_ = done_bb;
  coro_resume_bb_ = resume_bb;
  coro_await_emitted_ = false;
  coro_await_cont_bb_ = nullptr;

  // ---- Bind parameters and locals to their frame slots ----
  named_values_.clear();
  local_types_.clear();
  closure_locals_.clear();
  fat_closure_locals_.clear();
  fat_closure_ctx_tys_.clear();
  heap_locals_.clear();
  owned_string_locals_.clear();
  arena_locals_.clear();
  readonly_refs_.clear();
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  held_sync_locks_.clear();
  in_coroutine_ = true;
  current_module_ = fn_ast.module_name;
  current_fn_return_type_ = fn_ast.return_type;
  current_fn_name_ = fn_ast.name;
  inject_deinit_on_return_ = false;

  // Locals are addressed through their frame slots; `createEntryAlloca` would
  // otherwise put them on the stack where they die at the suspend point.

  builder_.SetInsertPoint(body_bb);
  enterDeferScope();
  for (const ast::Param& param : fn_ast.params) {
    auto it = coro_local_slots_.find(param.name);
    if (it == coro_local_slots_.end()) {
      continue;
    }
    const std::string ptn = resolveTypeName(param.type_name);
    llvm::Value* slot = coro_slot_addrs_[param.name];
    named_values_[param.name] = slot;
    local_types_[param.name] = ptn;
    if (isClassType(ptn)) {
      heap_locals_.insert(param.name);
    }
  }

  if (!emitStmtList(fn_ast.body)) {
    in_coroutine_ = false;
    coro_frame_ty_ = nullptr;
    return false;
  }
  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    if (!finishCleanups()) {
      in_coroutine_ = false;
      coro_frame_ty_ = nullptr;
      return false;
    }
    builder_.CreateBr(done_bb);
  }

  // ---- Completion: publish the result, retire the task ----
  builder_.SetInsertPoint(done_bb);
  llvm::Value* task = builder_.CreateLoad(
      i8p, builder_.CreateStructGEP(coro_frame_ty_, frame, kTaskIdx, "task.slot"), "task");
  llvm::Value* result_ptr =
      builder_.CreateStructGEP(coro_frame_ty_, frame, kResultIdx, "result.ptr");
  builder_.CreateCall(set_result_fn, {task, result_ptr});
  builder_.CreateCall(complete_fn, {task});
  builder_.CreateRetVoid();

  // State 1 (post-`await`) resumes at the continuation of the awaited call.
  if (coro_await_cont_bb_ && coro_resume_bb_) {
    builder_.SetInsertPoint(coro_resume_bb_);
    builder_.CreateBr(coro_await_cont_bb_);
  }
  // An async function with no `await` never enters the resume block.
  if (resume_bb->use_empty()) {
    resume_bb->eraseFromParent();
  }

  defer_scopes_.clear();
  auto_free_scopes_.clear();
  held_sync_locks_.clear();
  in_coroutine_ = false;
  coro_frame_ty_ = nullptr;
  coro_frame_ = nullptr;
  coro_done_bb_ = nullptr;
  coro_resume_bb_ = nullptr;

  if (!fn->empty() && llvm::verifyFunction(*step_fn, &llvm::errs())) {
    error(fn_ast.loc, "async state machine verification failed: " + fn_ast.name);
    return false;
  }
  if (llvm::verifyFunction(*fn, &llvm::errs())) {
    error(fn_ast.loc, "async entry wrapper verification failed: " + fn_ast.name);
    return false;
  }
  return true;
}

void Codegen::publishCoroResult(llvm::Value* value) {
  // `return` inside an async body: copy the value into the frame's result slot
  // (the `done` block publishes it) and jump there.
  if (value && coro_frame_) {
    llvm::Type* want = coro_frame_ty_->getElementType(coro_result_idx_);
    llvm::Value* stored = value;
    if (stored->getType() != want) {
      if (want->isIntegerTy() && stored->getType()->isIntegerTy()) {
        stored = builder_.CreateIntCast(stored, want, /*isSigned=*/true, "result.cast");
      } else if (want->isPointerTy() && stored->getType()->isPointerTy()) {
        stored = builder_.CreateBitCast(stored, want, "result.cast");
      } else if (want->isDoubleTy() && stored->getType()->isIntegerTy()) {
        stored = builder_.CreateSIToFP(stored, want, "result.cast");
      } else if (want->isIntegerTy() && stored->getType()->isDoubleTy()) {
        stored = builder_.CreateFPToSI(stored, want, "result.cast");
      }
    }
    builder_.CreateStore(stored,
                         builder_.CreateStructGEP(coro_frame_ty_, coro_frame_, coro_result_idx_,
                                                  "result.ptr"));
  }
  emitCoroReturn();
}

void Codegen::emitCoroReturn() {
  if (coro_done_bb_) {
    builder_.CreateBr(coro_done_bb_);
  }
}

llvm::Value* Codegen::emitAwaitExpr(const ast::AwaitExpr& expr) {
  if (!in_coroutine_) {
    error(expr.loc, "'await' is only allowed inside an 'async fun'");
    return nullptr;
  }
  if (coro_await_emitted_) {
    error(expr.loc, "only one 'await' per async function is supported (async MVP)");
    return nullptr;
  }
  llvm::Type* i8p = ptrTy();
  llvm::Type* i32 = i32Ty();

  // 1) Start the awaited task (async calls return a task handle directly).
  llvm::Value* inner = emitExpr(*expr.task);
  if (!inner) {
    return nullptr;
  }
  if (!inner->getType()->isPointerTy()) {
    error(expr.loc, "'await' expects a task handle (ptr)");
    return nullptr;
  }
  llvm::FunctionCallee enqueue_fn =
      module_->getOrInsertFunction("l_task_enqueue",
                                   llvm::FunctionType::get(llvm::Type::getVoidTy(globalContext()),
                                                           {i8p}, false));

  // 2) Payload type comes from the awaited call's declared return type.
  std::string awaited_ty;
  if (expr.task && expr.task->kind() == ast::ExprKind::Call) {
    const auto& call = static_cast<const ast::CallExpr&>(*expr.task);
    auto it = concrete_funcs_.find(call.callee);
    if (it == concrete_funcs_.end() && !current_module_.empty()) {
      it = concrete_funcs_.find(current_module_ + "::" + call.callee);
    }
    if (it != concrete_funcs_.end() && it->second) {
      awaited_ty = resolveTypeName(it->second->return_type);
    }
  }
  llvm::Type* val_ty = awaited_ty.empty() || awaited_ty == "void" ? i32 : typeFromName(awaited_ty);
  if (!val_ty || val_ty->isVoidTy()) {
    error(expr.loc, "cannot await a task with no result type");
    return nullptr;
  }

  // 3) Suspend: save the resume state, end this step, and continue in the
  //    continuation block when the executor resumes the frame at state 1.
  //    A PHI in the continuation carries the awaited value across the boundary
  //    (the value is produced *after* the resume, from the task's result slot).
  llvm::Function* step_fn = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* cont_bb =
      llvm::BasicBlock::Create(globalContext(), "await.cont", step_fn);
  coro_await_cont_bb_ = cont_bb;
  builder_.CreateStore(llvm::ConstantInt::get(i32, 1),
                       builder_.CreateStructGEP(coro_frame_ty_, coro_frame_, 0, "state.ptr"));
  if (coro_dispatch_sw_) {
    coro_dispatch_sw_->addCase(llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(i32, 1)),
                               coro_resume_bb_);
  }
  // Park the awaited handle in a dedicated frame slot so the continuation (and
  // the resumed state-1 step) can read it without needing SSA dominance.
  llvm::Value* parked = builder_.CreateStructGEP(coro_frame_ty_, coro_frame_,
                                                 coro_await_task_idx_, "await.task.ptr");
  builder_.CreateStore(inner, parked);
  builder_.CreateBr(cont_bb);

  // ---- Continuation: state 1 lands here ----
  builder_.SetInsertPoint(cont_bb);
  llvm::Value* task_h =
      builder_.CreateLoad(i8p,
                          builder_.CreateStructGEP(coro_frame_ty_, coro_frame_,
                                                   coro_await_task_idx_, "await.task.ptr"),
                          "await.task");
  llvm::FunctionCallee await_fn =
      module_->getOrInsertFunction("l_task_await", llvm::FunctionType::get(i8p, {i8p}, false));
  llvm::Value* res_ptr = builder_.CreateCall(await_fn, {task_h}, "await.result");
  llvm::Value* slot =
      builder_.CreateBitCast(res_ptr, llvm::PointerType::get(val_ty, 0), "await.slot");
  llvm::Value* loaded = builder_.CreateLoad(val_ty, slot, "await.val");
  coro_await_emitted_ = true;
  (void)i32;
  (void)enqueue_fn;
  return loaded;
}
bool Codegen::emitFuncDecl(const ast::FuncDeclStmt& fn_ast) {
  if (fn_ast.is_method || fn_ast.is_extern) {
    return true;
  }
  if (fn_ast.name == "__compile_time_handler" || fn_ast.name == "__on_class_parsed") {
    return true;  // frontend LCMD interpreter only
  }
  if (!fn_ast.generics.empty()) {
    return true;  // template — specialized on demand
  }
  if (fn_ast.is_async) {
    return emitAsyncFuncDecl(fn_ast);
  }
  const std::string ir_name = mangleName(fn_ast.module_name, fn_ast.name, fn_ast.is_pub);
  llvm::Function* fn = module_->getFunction(ir_name);
  if (!fn) {
    error(fn_ast.loc, "missing function declaration: " + ir_name);
    return false;
  }
  if (!fn->empty()) {
    error(fn_ast.loc, "redefinition of function: " + ir_name);
    return false;
  }

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  named_values_.clear();
  local_types_.clear();
  closure_locals_.clear();
  fat_closure_locals_.clear();
  fat_closure_ctx_tys_.clear();
  heap_locals_.clear();
  owned_string_locals_.clear();
  arena_locals_.clear();
  readonly_refs_.clear();
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  held_sync_locks_.clear();
  current_module_ = fn_ast.module_name;
  current_fn_return_type_ = fn_ast.return_type;
  current_fn_name_ = fn_ast.name;
  const bool is_main = (fn_ast.name == "main");
  inject_deinit_on_return_ = is_main && global_deinit_fn_ != nullptr;
  // Debug info: bind the lexical function + its first DILocation so every
  // instruction until the next statement carries `fn_ast.loc`.
  if (di_builder_) {
    di_sp_ = debugSubprogram(fn, fn_ast, debugFile(fn_ast.loc.filename));
    setDebugLoc(fn_ast.loc);
  }
  enterDeferScope();

  if (is_main) {
    // LLVM main is always (i32 argc, ptr argv); stash when extern l_init_args is present.
    if (fn->arg_size() != 2) {
      error(fn_ast.loc, "internal: main must be declared as (i32, ptr) at the LLVM layer");
      return false;
    }
    llvm::Argument& argc_arg = *fn->arg_begin();
    llvm::Argument& argv_arg = *std::next(fn->arg_begin());
    argc_arg.setName("argc");
    argv_arg.setName("argv");
    // Only call if declared via `extern module` (stdlib/io.l) — no hardcoded declare.
    if (llvm::Function* init_args = module_->getFunction("l_init_args")) {
      builder_.CreateCall(init_args, {&argc_arg, &argv_arg});
    }

    if (fn_ast.params.size() == 2) {
      bindParam(fn, &argc_arg, fn_ast.params[0], fn_ast.params[0].type_name);
      bindParam(fn, &argv_arg, fn_ast.params[1], fn_ast.params[1].type_name);
    } else if (!fn_ast.params.empty()) {
      error(fn_ast.loc, "main must have zero parameters or (i32 argc, ptr argv)");
      return false;
    }
  } else {
    unsigned idx = 0;
    for (auto& arg : fn->args()) {
      if (idx >= fn_ast.params.size()) {
        error(fn_ast.loc, "internal: argument count mismatch for " + fn_ast.name);
        return false;
      }
      const ast::Param& param = fn_ast.params[idx];
      arg.setName(param.name);
      bindParam(fn, &arg, param, param.type_name);
      ++idx;
    }
  }

  // Lifecycle: run module __init at the very start of main.
  if (is_main && global_init_fn_) {
    builder_.CreateCall(global_init_fn_);
  }

  if (!emitStmtList(fn_ast.body)) {
    current_fn_return_type_.clear();
    held_sync_locks_.clear();
    inject_deinit_on_return_ = false;
    return false;
  }

  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    if (!finishCleanups()) {
      current_fn_return_type_.clear();
      inject_deinit_on_return_ = false;
      return false;
    }
    emitLifecycleDeinitCall();
    if (fn->getReturnType()->isVoidTy()) {
      builder_.CreateRetVoid();
    } else if (fn->getReturnType()->isIntegerTy(32)) {
      builder_.CreateRet(llvm::ConstantInt::get(i32Ty(), 0));
    } else {
      error(fn_ast.loc, "missing return in non-i32 function: " + fn_ast.name);
      current_fn_return_type_.clear();
      inject_deinit_on_return_ = false;
      return false;
    }
  }
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  held_sync_locks_.clear();
  current_fn_return_type_.clear();
  inject_deinit_on_return_ = false;

  if (llvm::verifyFunction(*fn, &llvm::errs())) {
    error(fn_ast.loc, "function verification failed: " + fn_ast.name);
    fn->eraseFromParent();
    return false;
  }
  return true;
}

bool Codegen::initClassAtPtr(llvm::Value* base, const ClassInfo& info,
                             const ast::NewExpr& neu) {
  // Always zero-fill first so fields have defined values before / without init.
  for (size_t i = 0; i < info.field_types.size(); ++i) {
    llvm::Type* fty = llvmTypeForTypeName(info.field_types[i]);
    llvm::Value* gep =
        builder_.CreateStructGEP(info.type, base, static_cast<unsigned>(i), "ctor.zero");
    llvm::Value* zero = llvm::Constant::getNullValue(fty);
    builder_.CreateStore(zero, gep);
  }

  // [link:97-Implicit-Delegation]: embedded fields are anonymous class instances; construct each
  // one before the user's init runs so promoted method calls find a live object.
  if (info.ast) {
    for (size_t i = 0; i < info.ast->fields.size() && i < info.field_types.size(); ++i) {
      const auto& f = info.ast->fields[i];
      if (!f || !f->is_delegate) {
        continue;
      }
      const std::string& fty_name = info.field_types[i];
      if (fty_name.empty() || !isClassType(fty_name)) {
        continue;
      }
      ensureTypeAvailable(fty_name);
      const ClassInfo* finfo = findClass(fty_name);
      if (!finfo || !finfo->type) {
        continue;
      }
      llvm::Value* slot =
          builder_.CreateStructGEP(info.type, base, static_cast<unsigned>(i), "delegate.slot");
      llvm::Type* slot_ty = info.type->getElementType(i);
      // Inline struct field: initialize in place. Pointer field: heap-allocate.
      llvm::Value* emb = slot;
      if (slot_ty && slot_ty->isPointerTy()) {
        ensureDataLayout();
        llvm::Function* alloc_fn = requireExtern("l_alloc", info.ast->loc);
        if (!alloc_fn) {
          continue;
        }
        const uint64_t bytes = module_->getDataLayout().getTypeAllocSize(finfo->type);
        llvm::Value* sz = llvm::ConstantInt::get(i64Ty(), bytes, false);
        llvm::Value* raw = builder_.CreateCall(alloc_fn, {sz}, fty_name + ".delegate");
        emb = builder_.CreateBitCast(raw, ptrTy(), fty_name + ".delegate.ptr");
        builder_.CreateStore(emb, slot);
      }
      builder_.CreateStore(llvm::Constant::getNullValue(finfo->type), emb);
      auto emb_init = finfo->methods.find("init");
      if (emb_init != finfo->methods.end() && emb_init->second &&
          emb_init->second->arg_size() == 1) {
        builder_.CreateCall(emb_init->second, {emb});
      }
    }
  }

  // Preferred path: custom constructor `fun init(...)` with implicit this*.
  auto init_it = info.methods.find("init");
  if (init_it != info.methods.end()) {
    const ast::FuncDeclStmt* init_ast = nullptr;
    if (info.ast) {
      for (const auto& m : info.ast->methods) {
        if (m && m->name == "init") {
          init_ast = m.get();
          break;
        }
      }
    }
    return emitInitArgsWith(info, neu, init_it->second, init_ast, base, neu.args) != nullptr;
  }

  // Legacy fallback: map NewExpr args onto fields in declaration order.
  if (neu.args.size() > info.field_types.size()) {
    error(neu.loc, "too many constructor arguments for " + neu.class_name);
    return false;
  }
  for (size_t i = 0; i < neu.args.size(); ++i) {
    if (!neu.args[i].value) {
      continue;
    }
    if (!neu.args[i].name.empty()) {
      error(neu.args[i].value->loc, "keyword arguments not supported in field-order constructors");
      return false;
    }
    llvm::Type* fty = llvmTypeForTypeName(info.field_types[i]);
    llvm::Value* v = nullptr;
    // `Result<..., String>(..., "msg")` — coerce string literal → String by value.
    if (info.field_types[i] == "String" &&
        neu.args[i].value->kind() == ast::ExprKind::StringLiteral) {
      llvm::Value* sp = emitStringObjectPtr(*neu.args[i].value);
      if (!sp) {
        return false;
      }
      v = builder_.CreateLoad(fty, sp, "ctor.str.copy");
    } else {
      v = emitExpr(*neu.args[i].value);
      if (!v) {
        return false;
      }
      if (fty->isIntegerTy(1)) {
        v = toI1(v);
      } else if (fty->isIntegerTy(32) && !v->getType()->isPointerTy()) {
        v = toI32(v, neu.loc);
      } else if (isInterfaceType(info.field_types[i])) {
        // Interface field (e.g. Optional<IPlugin>.value): box Class → fat pointer.
        v = emitValueForParam(*neu.args[i].value, fty, info.field_types[i]);
        if (!v) {
          return false;
        }
      } else if (fty->isStructTy() && v->getType()->isPointerTy()) {
        // Nested class field (e.g. Optional<String>.value_): NewExpr yields Class*.
        v = builder_.CreateLoad(fty, v, "ctor.field.copy");
      }
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

llvm::Value* Codegen::emitClassInitCall(const ClassInfo& info, const ast::NewExpr& neu,
                                        llvm::Value* this_ptr,
                                        const std::vector<ast::CallArg>& args) {
  auto init_it = info.methods.find("init");
  if (init_it == info.methods.end() || !init_it->second) {
    error(neu.loc, "class '" + info.ir_name + "' has no init to call");
    return nullptr;
  }
  // Call args are borrowed (`ast::CallArg` has a unique_ptr and no copy), so the
  // init signature AST is only needed for type-name hints — skip it here.
  return emitInitArgsWith(info, neu, init_it->second, nullptr, this_ptr, args);
}

// Shared arg lowering for constructors: positional/keyword args → init params.
llvm::Value* Codegen::emitInitArgsWith(const ClassInfo& info, const ast::NewExpr& neu,
                                       llvm::Function* init_fn,
                                       const ast::FuncDeclStmt* init_ast, llvm::Value* base,
                                       const std::vector<ast::CallArg>& args) {
  if (init_fn->arg_size() != args.size() + 1) {
    error(neu.loc, "argument count mismatch calling " + neu.class_name + ".init");
    return nullptr;
  }
  std::vector<llvm::Value*> call_args;
  call_args.push_back(base);
  for (size_t i = 0; i < args.size(); ++i) {
    const ast::CallArg& arg = args[i];
    if (!arg.value) {
      error(neu.loc, "null argument to " + neu.class_name + ".init");
      return nullptr;
    }
    if (!arg.name.empty()) {
      error(neu.loc, "keyword arguments not supported in constructors yet");
      return nullptr;
    }
    llvm::Type* expected = init_fn->getFunctionType()->getParamType(static_cast<unsigned>(i + 1));
    std::string expected_name;
    if (init_ast && i < init_ast->params.size()) {
      expected_name = instantiateMemberType(init_ast->params[i].type_name, info);
    }
    llvm::Value* v = emitValueForParam(*arg.value, expected, expected_name);
    if (!v) {
      return nullptr;
    }
    call_args.push_back(v);
  }
  return builder_.CreateCall(init_fn, call_args);
}

// Copy a class value from its stack slot into a destination stack slot.
bool Codegen::initStackClassFromPtr(llvm::Value* alloca, const ClassInfo& info,
                                    llvm::Value* src_ptr, const std::string& name) {
  // Any source whose slot holds the class itself is a plain value copy; the
  // cloning `init(this, other)` is only for sources with no class-typed storage.
  if (localSlotType(src_ptr) == info.type) {
    ensureDataLayout();
    builder_.CreateMemCpy(alloca, llvm::MaybeAlign(),
                          builder_.CreateBitCast(src_ptr, ptrTy(), name + ".raw"),
                          llvm::MaybeAlign(),
                          module_->getDataLayout().getTypeAllocSize(info.type));
    return true;
  }
  auto init_it = info.methods.find("init");
  if (init_it != info.methods.end() && init_it->second && init_it->second->arg_size() == 2) {
    builder_.CreateCall(init_it->second, {alloca, src_ptr}, "");
    return true;
  }
  ensureDataLayout();
  const uint64_t bytes = module_->getDataLayout().getTypeAllocSize(info.type);
  llvm::Value* src_raw = builder_.CreateBitCast(src_ptr, ptrTy(), name + ".raw");
  builder_.CreateMemCpy(alloca, llvm::MaybeAlign(), src_raw, llvm::MaybeAlign(), bytes);
  return true;
}

// Heap-allocate a class for the auto-free path (`drop()` owners).
llvm::Value* Codegen::emitHeapClassAllocAndInit(const ClassInfo& info, const ast::NewExpr& neu,
                                                const std::string& name) {
  ensureDataLayout();
  llvm::Function* alloc_fn = requireExtern("l_alloc", neu.loc);
  if (!alloc_fn) {
    return nullptr;
  }
  const uint64_t bytes = module_->getDataLayout().getTypeAllocSize(info.type);
  llvm::Value* raw =
      builder_.CreateCall(alloc_fn, {llvm::ConstantInt::get(i64Ty(), bytes)}, name + ".heap");
  if (!initClassAtPtr(raw, info, neu)) {
    return nullptr;
  }
  return raw;
}

// Build a stack class value in `slot` (literal String, NewExpr, or class-typed expr).
bool Codegen::emitStackClassInit(llvm::AllocaInst* slot, const ClassInfo& info,
                                 const ast::VarDeclStmt& decl) {
  const ast::Expr& init = *decl.init;
  if (init.kind() == ast::ExprKind::New) {
    const auto& neu = static_cast<const ast::NewExpr&>(init);
    if (!initClassFromNew(slot, info, neu)) {
      return false;
    }
    return true;
  }
  // `s := "hello"` → stack String initialized from a C string literal.
  // NOTE: `decl.type_name` is empty for `:=` inferred declarations, so the
  // resolved class (`info.ir_name`) is the authoritative check.
  const bool is_string_class = decl.type_name == "String" || info.ir_name == "string_String";
  if (is_string_class && init.kind() == ast::ExprKind::StringLiteral) {
    auto init_it = info.methods.find("init");
    if (init_it == info.methods.end()) {
      error(decl.loc, "String.init not found");
      return false;
    }
    for (size_t i = 0; i < info.field_types.size(); ++i) {
      llvm::Value* gep = builder_.CreateStructGEP(info.type, slot, static_cast<unsigned>(i),
                                                  decl.name + ".zero");
      builder_.CreateStore(llvm::Constant::getNullValue(llvmTypeForTypeName(info.field_types[i])),
                           gep);
    }
    const auto& lit = static_cast<const ast::StringLiteralExpr&>(init);
    llvm::Value* cstr = builder_.CreateGlobalString(lit.value);
    builder_.CreateCall(init_it->second, {slot, cstr}, "");
    return true;
  }
  // A namespaced call returning a String value (`s := mod::greet(x)`): store the
  // returned struct directly — taking its address would load from a register value.
  if (is_string_class && init.kind() == ast::ExprKind::ScopedCall) {
    llvm::Value* v = emitExpr(init);
    if (!v) {
      return false;
    }
    if (v->getType() != info.type) {
      error(decl.loc, "namespaced call for '" + decl.name + "' must yield a String value");
      return false;
    }
    builder_.CreateStore(v, slot);
    return true;
  }
  // Method calls / String+ / other class-typed expressions.
  // [link:50-Variant] A Variant source unboxes to String first when that's the target.
  llvm::Value* v = emitContextualVariantUnbox(init, decl.type_name, nullptr, /*immediate=*/true);
  if (!v) {
    v = emitExpr(init);
  }
  if (!v) {
    return false;
  }
  if (v->getType() == info.type) {
    builder_.CreateStore(v, slot);
    return true;
  }
  if (!v->getType()->isPointerTy()) {
    error(decl.loc, "cannot initialize class '" + decl.name + "' from non-class expression");
    return false;
  }
  // Any class-typed pointer (a local slot, or an Optional payload slot) points at
  // class storage: copy the bytes through an opaque pointer.
  ensureDataLayout();
  builder_.CreateMemCpy(slot, llvm::MaybeAlign(),
                        builder_.CreateBitCast(v, ptrTy(), decl.name + ".src"),
                        llvm::MaybeAlign(),
                        module_->getDataLayout().getTypeAllocSize(info.type));
  return true;
}

// `init(this*, args...)` with a resolved this*.
bool Codegen::emitInitArgs(const ClassInfo& info, const ast::NewExpr& neu,
                           llvm::Function* init_fn, llvm::Value* base) {
  return emitInitArgsWith(info, neu, init_fn, nullptr, base, neu.args) != nullptr;
}

bool Codegen::emitDefer(const ast::DeferStmt& stmt) {
  if (defer_scopes_.empty()) {
    enterDeferScope();
  }
  defer_scopes_.back().push_back(&stmt);
  // Explicit `defer l_free(p)` opts out of auto-free to avoid double free.
  noteExplicitLFreeInDefer(stmt);
  return true;
}

bool Codegen::emitSync(const ast::SyncBlockStmt& stmt) {
  // sync { } → lock process-wide mutex, run body, unlock (also via finishCleanups on return).
  llvm::Function* global_fn = requireExtern("l_mutex_global", stmt.loc);
  llvm::Function* lock_fn = requireExtern("l_mutex_lock", stmt.loc);
  llvm::Function* unlock_fn = requireExtern("l_mutex_unlock", stmt.loc);
  if (!global_fn || !lock_fn || !unlock_fn) {
    return false;
  }
  llvm::Value* mu = builder_.CreateCall(global_fn, {}, "sync.mu");
  builder_.CreateCall(lock_fn, {mu});
  held_sync_locks_.push_back(mu);

  if (!emitStmtListScoped(stmt.body)) {
    return false;
  }

  // Normal exit: unlock this sync frame (early return uses finishCleanups).
  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateCall(unlock_fn, {mu});
    if (!held_sync_locks_.empty() && held_sync_locks_.back() == mu) {
      held_sync_locks_.pop_back();
    }
  }
  return true;
}

void Codegen::collectSpawnLocalDecls(const ast::Stmt& stmt,
                                     std::unordered_set<std::string>& locals) const {
  switch (stmt.kind()) {
    case ast::StmtKind::VarDecl: {
      const auto& d = static_cast<const ast::VarDeclStmt&>(stmt);
      if (!d.name.empty()) {
        locals.insert(d.name);
      }
      break;
    }
    case ast::StmtKind::DestructureDecl: {
      const auto& d = static_cast<const ast::DestructureDeclStmt&>(stmt);
      for (const auto& b : d.bindings) {
        if (!b.name.empty()) {
          locals.insert(b.name);
        }
      }
      break;
    }
    case ast::StmtKind::Block: {
      const auto& b = static_cast<const ast::BlockStmt&>(stmt);
      for (const ast::StmtPtr& s : b.stmts) {
        if (s) collectSpawnLocalDecls(*s, locals);
      }
      break;
    }
    case ast::StmtKind::If: {
      const auto& i = static_cast<const ast::IfStmt&>(stmt);
      for (const ast::StmtPtr& s : i.then_body) {
        if (s) collectSpawnLocalDecls(*s, locals);
      }
      for (const ast::StmtPtr& s : i.else_body) {
        if (s) collectSpawnLocalDecls(*s, locals);
      }
      break;
    }
    case ast::StmtKind::While: {
      const auto& w = static_cast<const ast::WhileStmt&>(stmt);
      for (const ast::StmtPtr& s : w.body) {
        if (s) collectSpawnLocalDecls(*s, locals);
      }
      break;
    }
    case ast::StmtKind::ForIn: {
      const auto& f = static_cast<const ast::ForInStmt&>(stmt);
      if (!f.item_name.empty()) {
        locals.insert(f.item_name);
      }
      for (const ast::StmtPtr& s : f.body) {
        if (s) collectSpawnLocalDecls(*s, locals);
      }
      break;
    }
    case ast::StmtKind::Match: {
      const auto& m = static_cast<const ast::MatchStmt&>(stmt);
      for (const ast::MatchBranch& br : m.branches) {
        for (const ast::StmtPtr& s : br.body) {
          if (s) collectSpawnLocalDecls(*s, locals);
        }
      }
      break;
    }
    case ast::StmtKind::Sync: {
      const auto& s = static_cast<const ast::SyncBlockStmt&>(stmt);
      for (const ast::StmtPtr& b : s.body) {
        if (b) collectSpawnLocalDecls(*b, locals);
      }
      break;
    }
    case ast::StmtKind::Spawn: {
      const auto& s = static_cast<const ast::SpawnBlockStmt&>(stmt);
      for (const ast::StmtPtr& b : s.body) {
        if (b) collectSpawnLocalDecls(*b, locals);
      }
      break;
    }
    case ast::StmtKind::Defer: {
      const auto& d = static_cast<const ast::DeferStmt&>(stmt);
      for (const ast::StmtPtr& b : d.body) {
        if (b) collectSpawnLocalDecls(*b, locals);
      }
      break;
    }
    default:
      break;
  }
}

void Codegen::collectSpawnCaptures(const ast::Expr& expr, std::vector<std::string>& out,
                                   std::unordered_set<std::string>& seen,
                                   const std::unordered_set<std::string>* body_locals) const {
  switch (expr.kind()) {
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      if (body_locals && body_locals->count(id.name) != 0) {
        break;
      }
      if (named_values_.count(id.name) != 0 && seen.insert(id.name).second) {
        out.push_back(id.name);
      }
      break;
    }
    case ast::ExprKind::Coalesce: {
      const auto& co = static_cast<const ast::CoalesceExpr&>(expr);
      if (co.left) collectSpawnCaptures(*co.left, out, seen, body_locals);
      if (co.right) collectSpawnCaptures(*co.right, out, seen, body_locals);
      break;
    }
    case ast::ExprKind::Binary: {
      const auto& bin = static_cast<const ast::BinaryExpr&>(expr);
      if (bin.left) collectSpawnCaptures(*bin.left, out, seen, body_locals);
      if (bin.right) collectSpawnCaptures(*bin.right, out, seen, body_locals);
      break;
    }
    case ast::ExprKind::Call: {
      const auto& call = static_cast<const ast::CallExpr&>(expr);
      // `cb()` — callee string may be a captured local (closure / fun ptr / fat closure).
      if (!call.callee.empty() && named_values_.count(call.callee) != 0 &&
          !(body_locals && body_locals->count(call.callee) != 0) &&
          seen.insert(call.callee).second) {
        out.push_back(call.callee);
      }
      for (const ast::CallArg& a : call.args) {
        if (a.value) collectSpawnCaptures(*a.value, out, seen, body_locals);
      }
      break;
    }
    case ast::ExprKind::MemberAccess: {
      const auto& m = static_cast<const ast::MemberAccessExpr&>(expr);
      if (m.object) collectSpawnCaptures(*m.object, out, seen, body_locals);
      break;
    }
    case ast::ExprKind::MethodCall: {
      const auto& m = static_cast<const ast::MethodCallExpr&>(expr);
      if (m.object) collectSpawnCaptures(*m.object, out, seen, body_locals);
      for (const ast::CallArg& a : m.args) {
        if (a.value) collectSpawnCaptures(*a.value, out, seen, body_locals);
      }
      break;
    }
    case ast::ExprKind::ScopedCall: {
      const auto& sc = static_cast<const ast::ScopedCallExpr&>(expr);
      for (const ast::CallArg& a : sc.args) {
        if (a.value) collectSpawnCaptures(*a.value, out, seen, body_locals);
      }
      break;
    }
    case ast::ExprKind::Index: {
      const auto& ix = static_cast<const ast::IndexExpr&>(expr);
      if (ix.object) collectSpawnCaptures(*ix.object, out, seen, body_locals);
      if (ix.index) collectSpawnCaptures(*ix.index, out, seen, body_locals);
      break;
    }
    case ast::ExprKind::Cast: {
      const auto& c = static_cast<const ast::CastExpr&>(expr);
      if (c.value) collectSpawnCaptures(*c.value, out, seen, body_locals);
      break;
    }
    case ast::ExprKind::IncDec: {
      const auto& inc = static_cast<const ast::IncDecExpr&>(expr);
      if (inc.target) collectSpawnCaptures(*inc.target, out, seen, body_locals);
      break;
    }
    case ast::ExprKind::Try: {
      const auto& t = static_cast<const ast::TryExpr&>(expr);
      if (t.inner) collectSpawnCaptures(*t.inner, out, seen, body_locals);
      break;
    }
    case ast::ExprKind::Spawn: {
      const auto& s = static_cast<const ast::SpawnExpr&>(expr);
      for (const ast::StmtPtr& b : s.body) {
        if (b) collectSpawnCaptures(*b, out, seen, body_locals);
      }
      break;
    }
    case ast::ExprKind::Heap: {
      const auto& h = static_cast<const ast::HeapExpr&>(expr);
      if (h.inner) collectSpawnCaptures(*h.inner, out, seen, body_locals);
      break;
    }
    case ast::ExprKind::New: {
      const auto& n = static_cast<const ast::NewExpr&>(expr);
      for (const ast::CallArg& a : n.args) {
        if (a.value) collectSpawnCaptures(*a.value, out, seen, body_locals);
      }
      break;
    }
    case ast::ExprKind::ArrayLiteral: {
      const auto& lit = static_cast<const ast::ArrayLiteralExpr&>(expr);
      for (const ast::ExprPtr& e : lit.elements) {
        if (e) collectSpawnCaptures(*e, out, seen, body_locals);
      }
      break;
    }
    case ast::ExprKind::Lambda: {
      const auto& lam = static_cast<const ast::LambdaExpr&>(expr);
      for (const ast::StmtPtr& s : lam.body) {
        if (s) collectSpawnCaptures(*s, out, seen, body_locals);
      }
      break;
    }
    default:
      break;
  }
}

void Codegen::collectSpawnCaptures(const ast::Stmt& stmt, std::vector<std::string>& out,
                                   std::unordered_set<std::string>& seen,
                                   const std::unordered_set<std::string>* body_locals) const {
  switch (stmt.kind()) {
    case ast::StmtKind::VarDecl: {
      const auto& d = static_cast<const ast::VarDeclStmt&>(stmt);
      if (d.init) collectSpawnCaptures(*d.init, out, seen, body_locals);
      break;
    }
    case ast::StmtKind::DestructureDecl: {
      const auto& d = static_cast<const ast::DestructureDeclStmt&>(stmt);
      if (d.init) collectSpawnCaptures(*d.init, out, seen, body_locals);
      break;
    }
    case ast::StmtKind::Assign: {
      const auto& a = static_cast<const ast::AssignStmt&>(stmt);
      if (a.target) collectSpawnCaptures(*a.target, out, seen, body_locals);
      if (a.value) collectSpawnCaptures(*a.value, out, seen, body_locals);
      break;
    }
    case ast::StmtKind::Return: {
      const auto& r = static_cast<const ast::ReturnStmt&>(stmt);
      if (r.value) collectSpawnCaptures(*r.value, out, seen, body_locals);
      break;
    }
    case ast::StmtKind::ExprStmt: {
      const auto& e = static_cast<const ast::ExprStmt&>(stmt);
      if (e.expr) collectSpawnCaptures(*e.expr, out, seen, body_locals);
      break;
    }
    case ast::StmtKind::Block: {
      const auto& b = static_cast<const ast::BlockStmt&>(stmt);
      for (const ast::StmtPtr& s : b.stmts) {
        if (s) collectSpawnCaptures(*s, out, seen, body_locals);
      }
      break;
    }
    case ast::StmtKind::If: {
      const auto& i = static_cast<const ast::IfStmt&>(stmt);
      if (i.cond) collectSpawnCaptures(*i.cond, out, seen, body_locals);
      for (const ast::StmtPtr& s : i.then_body) {
        if (s) collectSpawnCaptures(*s, out, seen, body_locals);
      }
      for (const ast::StmtPtr& s : i.else_body) {
        if (s) collectSpawnCaptures(*s, out, seen, body_locals);
      }
      break;
    }
    case ast::StmtKind::While: {
      const auto& w = static_cast<const ast::WhileStmt&>(stmt);
      if (w.cond) collectSpawnCaptures(*w.cond, out, seen, body_locals);
      for (const ast::StmtPtr& s : w.body) {
        if (s) collectSpawnCaptures(*s, out, seen, body_locals);
      }
      break;
    }
    case ast::StmtKind::ForIn: {
      const auto& f = static_cast<const ast::ForInStmt&>(stmt);
      if (f.iterable) collectSpawnCaptures(*f.iterable, out, seen, body_locals);
      for (const ast::StmtPtr& s : f.body) {
        if (s) collectSpawnCaptures(*s, out, seen, body_locals);
      }
      break;
    }
    case ast::StmtKind::Match: {
      const auto& m = static_cast<const ast::MatchStmt&>(stmt);
      if (m.scrutinee) collectSpawnCaptures(*m.scrutinee, out, seen, body_locals);
      for (const ast::MatchBranch& br : m.branches) {
        if (br.pattern) collectSpawnCaptures(*br.pattern, out, seen, body_locals);
        for (const ast::StmtPtr& s : br.body) {
          if (s) collectSpawnCaptures(*s, out, seen, body_locals);
        }
      }
      break;
    }
    case ast::StmtKind::Sync: {
      const auto& s = static_cast<const ast::SyncBlockStmt&>(stmt);
      for (const ast::StmtPtr& b : s.body) {
        if (b) collectSpawnCaptures(*b, out, seen, body_locals);
      }
      break;
    }
    case ast::StmtKind::Spawn: {
      const auto& s = static_cast<const ast::SpawnBlockStmt&>(stmt);
      for (const ast::StmtPtr& b : s.body) {
        if (b) collectSpawnCaptures(*b, out, seen, body_locals);
      }
      break;
    }
    case ast::StmtKind::Defer: {
      const auto& d = static_cast<const ast::DeferStmt&>(stmt);
      for (const ast::StmtPtr& b : d.body) {
        if (b) collectSpawnCaptures(*b, out, seen, body_locals);
      }
      break;
    }
    default:
      break;
  }
}

bool Codegen::emitSpawn(const ast::SpawnBlockStmt& stmt) {
  // Statement form: fire-and-forget (caller may ignore the handle).
  return emitSpawnBody(stmt.body, stmt.loc) != nullptr;
}

llvm::Value* Codegen::emitSpawnBody(const std::vector<ast::StmtPtr>& body,
                                    const ast::SourceLocation& loc) {
  // Real OS thread: pack captured local addresses → __spawn_N(ctx) → l_spawn.
  // Does NOT join — returns an opaque handle for explicit `l_thread_join`.
  llvm::Function* spawn_rt = requireExtern("l_spawn", loc);
  if (!spawn_rt) {
    return nullptr;
  }

  std::unordered_set<std::string> body_locals;
  for (const ast::StmtPtr& s : body) {
    if (s) collectSpawnLocalDecls(*s, body_locals);
  }
  std::vector<std::string> caps;
  std::unordered_set<std::string> seen;
  for (const ast::StmtPtr& s : body) {
    if (s) collectSpawnCaptures(*s, caps, seen, &body_locals);
  }

  std::unordered_map<std::string, llvm::Value*> outer_slots;
  std::unordered_map<std::string, std::string> outer_cap_types;
  for (const std::string& name : caps) {
    auto nit = named_values_.find(name);
    if (nit == named_values_.end()) {
      error(loc, "spawn capture missing local: " + name);
      return nullptr;
    }
    outer_slots[name] = nit->second;
    auto lit = local_types_.find(name);
    outer_cap_types[name] = lit != local_types_.end() ? lit->second : "i32";
  }

  const unsigned spawn_id = ++spawn_seq_;
  llvm::StructType* ctx_ty = nullptr;
  if (!caps.empty()) {
    std::vector<llvm::Type*> fields(caps.size(), ptrTy());
    ctx_ty = llvm::StructType::create(globalContext(), fields,
                                      "__SpawnCtx_" + std::to_string(spawn_id));
  }

  auto* worker_ty = llvm::FunctionType::get(voidTy(), {ptrTy()}, /*isVarArg=*/false);
  const std::string wname = "__spawn_" + std::to_string(spawn_id);
  llvm::Function* worker =
      llvm::Function::Create(worker_ty, llvm::Function::InternalLinkage, wname, module_.get());

  auto saved_named = named_values_;
  auto saved_locals = local_types_;
  auto saved_heap = heap_locals_;
  auto saved_owned_strings = owned_string_locals_;
  auto saved_readonly = readonly_refs_;
  auto saved_defers = defer_scopes_;
  auto saved_autofree = auto_free_scopes_;
  auto saved_held_sync = held_sync_locks_;
  auto saved_class = current_class_;
  llvm::Value* saved_this = current_this_;
  auto saved_fn_ret = current_fn_return_type_;
  auto saved_cap_slots = capture_slots_;
  auto saved_cap_types = capture_types_;
  llvm::Value* saved_lam_ctx = current_lambda_ctx_;
  llvm::StructType* saved_lam_ctx_ty = current_lambda_ctx_ty_;
  const bool saved_lam_by_value = current_lambda_ctx_by_value_;
  llvm::BasicBlock* saved_insert = builder_.GetInsertBlock();
  llvm::BasicBlock::iterator saved_point =
      saved_insert ? builder_.GetInsertPoint() : llvm::BasicBlock::iterator();

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", worker);
  builder_.SetInsertPoint(entry);
  named_values_.clear();
  local_types_.clear();
  heap_locals_.clear();
  owned_string_locals_.clear();
  arena_locals_.clear();
  readonly_refs_.clear();
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  held_sync_locks_.clear();
  capture_slots_.clear();
  capture_types_.clear();
  current_class_.clear();
  current_this_ = nullptr;
  current_fn_return_type_.clear();
  enterDeferScope();

  llvm::Argument& ctx_arg = *worker->arg_begin();
  ctx_arg.setName("ctx");
  if (!caps.empty()) {
    current_lambda_ctx_ = &ctx_arg;
    current_lambda_ctx_ty_ = ctx_ty;
    current_lambda_ctx_by_value_ = false;  // spawn ctx stores ptrs to parent locals
    for (unsigned i = 0; i < caps.size(); ++i) {
      capture_slots_[caps[i]] = i;
      capture_types_[caps[i]] = outer_cap_types[caps[i]];
    }
  } else {
    current_lambda_ctx_ = nullptr;
    current_lambda_ctx_ty_ = nullptr;
    current_lambda_ctx_by_value_ = false;
  }

  bool ok = emitStmtList(body);
  if (ok && builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    if (!finishCleanups()) {
      ok = false;
    } else {
      // Capture ctx is heap-allocated so the parent can continue without joining;
      // free it after the worker body (and its deferred cleanups) finish.
      if (!caps.empty()) {
        llvm::Function* free_fn = requireExtern("l_free", loc);
        if (!free_fn) {
          ok = false;
        } else {
          builder_.CreateCall(free_fn, {&ctx_arg});
        }
      }
      if (ok) {
        builder_.CreateRetVoid();
      }
    }
  }

  if (ok && llvm::verifyFunction(*worker, &llvm::errs())) {
    error(loc, "spawn worker verification failed: " + wname);
    ok = false;
  }

  named_values_ = std::move(saved_named);
  local_types_ = std::move(saved_locals);
  heap_locals_ = std::move(saved_heap);
  owned_string_locals_ = std::move(saved_owned_strings);
  readonly_refs_ = std::move(saved_readonly);
  defer_scopes_ = std::move(saved_defers);
  auto_free_scopes_ = std::move(saved_autofree);
  held_sync_locks_ = std::move(saved_held_sync);
  current_class_ = std::move(saved_class);
  current_this_ = saved_this;
  current_fn_return_type_ = std::move(saved_fn_ret);
  capture_slots_ = std::move(saved_cap_slots);
  capture_types_ = std::move(saved_cap_types);
  current_lambda_ctx_ = saved_lam_ctx;
  current_lambda_ctx_ty_ = saved_lam_ctx_ty;
  current_lambda_ctx_by_value_ = saved_lam_by_value;
  if (saved_insert) {
    builder_.SetInsertPoint(saved_insert, saved_point);
  }
  if (!ok) {
    worker->eraseFromParent();
    return nullptr;
  }

  // Parent: heap-allocate ctx (survives past spawn return), then l_spawn — no auto-join.
  llvm::Value* ctx_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy()));
  if (!caps.empty()) {
    llvm::Function* alloc_fn = requireExtern("l_alloc", loc);
    if (!alloc_fn) {
      return nullptr;
    }
    ensureDataLayout();
    const uint64_t bytes = module_->getDataLayout().getTypeAllocSize(ctx_ty);
    llvm::Value* sz =
        llvm::ConstantInt::get(i64Ty(), static_cast<uint64_t>(bytes), /*isSigned=*/false);
    ctx_ptr = builder_.CreateCall(alloc_fn, {sz}, "spawn.ctx");
    for (unsigned i = 0; i < caps.size(); ++i) {
      llvm::Value* slot = outer_slots[caps[i]];
      // Store addresses of parent locals; worker loads through the same GEP layout.
      // Defensive: an unresolvable capture slot (escaping spawn closure) must not
      // reach the backend as a malformed GEP.
      if (!slot) {
        error(loc, "spawn capture '" + caps[i] + "' has no addressable local");
        return nullptr;
      }
      llvm::Value* gep =
          builder_.CreateStructGEP(ctx_ty, ctx_ptr, i, caps[i] + ".cap.slot");
      builder_.CreateStore(slot, gep);
    }
  }

  llvm::Value* fn_ptr = builder_.CreateBitCast(worker, ptrTy(), "spawn.fn");
  return builder_.CreateCall(spawn_rt, {fn_ptr, ctx_ptr}, "spawn.handle");
}

llvm::Value* Codegen::boxToInterface(llvm::Value* class_ptr, const std::string& class_name,
                                     const std::string& iface_name,
                                     const ast::SourceLocation& loc) {
  const ClassInfo* info = findClass(class_name);
  if (!info) {
    error(loc, "box: unknown class " + class_name);
    return nullptr;
  }
  auto vit = info->vtables.find(iface_name);
  if (vit == info->vtables.end()) {
    error(loc, "class " + class_name + " has no vtable for " + iface_name);
    return nullptr;
  }

  llvm::Value* data = builder_.CreateBitCast(class_ptr, ptrTy(), "iface.data");
  llvm::Value* vtable = builder_.CreateBitCast(vit->second, ptrTy(), "iface.vtable");

  llvm::Value* fat = llvm::UndefValue::get(fatPtrTy());
  fat = builder_.CreateInsertValue(fat, data, 0, "fat.data");
  fat = builder_.CreateInsertValue(fat, vtable, 1, "fat.vtable");
  return fat;
}

llvm::Value* Codegen::heapAllocNewExpr(const ast::NewExpr& neu) {
  if (!ensureTypeAvailable(neu.class_name)) {
    error(neu.loc, "unknown class in interface box: " + neu.class_name);
    return nullptr;
  }
  const ClassInfo* info = findClass(neu.class_name);
  if (!info || !info->type) {
    error(neu.loc, "unknown class in interface box: " + neu.class_name);
    return nullptr;
  }
  ensureDataLayout();
  llvm::Function* alloc = requireExtern("l_alloc", neu.loc);
  if (!alloc) {
    return nullptr;
  }
  const uint64_t bytes = module_->getDataLayout().getTypeAllocSize(info->type);
  llvm::Value* size =
      llvm::ConstantInt::get(i64Ty(), static_cast<uint64_t>(bytes), /*isSigned=*/false);
  llvm::Value* raw = builder_.CreateCall(alloc, {size}, "iface.heap");
  if (!initClassAtPtr(raw, *info, neu)) {
    return nullptr;
  }
  return raw;
}

llvm::Value* Codegen::emitIncDec(const ast::IncDecExpr& expr) {
  if (!expr.target) {
    error(expr.loc, "increment/decrement missing target");
    return nullptr;
  }
  llvm::Value* slot = emitLValue(*expr.target);
  if (!slot) {
    return nullptr;
  }

  std::string ty_name = "i32";
  if (expr.target->kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(*expr.target);
    auto lit = local_types_.find(id.name);
    if (lit != local_types_.end()) {
      ty_name = lit->second;
    } else {
      auto gt = global_types_.find(id.name);
      if (gt != global_types_.end()) {
        ty_name = gt->second;
      }
    }
  }
  llvm::Type* ty = llvmTypeForTypeName(ty_name);
  llvm::Value* old = builder_.CreateLoad(ty, slot, "incdec.old");

  // Postfix: spill old value so the expression result is stable after the store.
  llvm::Value* old_saved = old;
  if (!expr.is_prefix) {
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* tmp = createEntryAlloca(fn, ty, "incdec.tmp");
    builder_.CreateStore(old, tmp);
    old_saved = builder_.CreateLoad(ty, tmp, "incdec.old.saved");
  }

  llvm::Value* one = llvm::ConstantInt::get(ty, 1, /*isSigned=*/true);
  llvm::Value* neu =
      expr.is_inc ? builder_.CreateAdd(old, one, "incdec.new")
                  : builder_.CreateSub(old, one, "incdec.new");
  builder_.CreateStore(neu, slot);
  return expr.is_prefix ? neu : old_saved;
}

llvm::Value* Codegen::emitTry(const ast::TryExpr& expr) {
  // `inner?` → if !is_ok_, return Err Result from enclosing function; else yield ok_val_.
  if (!expr.inner) {
    error(expr.loc, "? missing operand");
    return nullptr;
  }
  if (current_fn_return_type_.empty()) {
    error(expr.loc, "? requires an enclosing function");
    return nullptr;
  }

  std::string inner_ty = inferTypeFromExpr(*expr.inner);
  if (inner_ty.empty()) {
    inner_ty = exprTypeOf(*expr.inner);
  }
  std::string inner_base;
  std::vector<std::string> inner_args;
  if (!splitGenericType(inner_ty, inner_base, inner_args) || inner_base != "Result" ||
      inner_args.size() != 2) {
    error(expr.loc, "? operand must be Result<T, E>, got '" + inner_ty + "'");
    return nullptr;
  }

  std::string fn_base;
  std::vector<std::string> fn_args;
  if (!splitGenericType(current_fn_return_type_, fn_base, fn_args) || fn_base != "Result" ||
      fn_args.size() != 2) {
    error(expr.loc, "? only allowed in functions returning Result, got '" +
                        current_fn_return_type_ + "'");
    return nullptr;
  }
  if (inner_args[1] != fn_args[1]) {
    error(expr.loc, "? error type mismatch: Result E is '" + inner_args[1] +
                        "' but function returns Result<..., " + fn_args[1] + ">");
    return nullptr;
  }

  if (!ensureTypeAvailable(inner_ty) || !ensureTypeAvailable(current_fn_return_type_)) {
    return nullptr;
  }
  const ClassInfo* inner_info = findClass(inner_ty);
  const ClassInfo* fn_info = findClass(current_fn_return_type_);
  if (!inner_info || !fn_info) {
    error(expr.loc, "Result type not available for ?");
    return nullptr;
  }
  auto is_ok_it = inner_info->field_index.find("is_ok_");
  auto ok_it = inner_info->field_index.find("ok_val_");
  auto err_it = inner_info->field_index.find("err_val_");
  if (is_ok_it == inner_info->field_index.end() || ok_it == inner_info->field_index.end() ||
      err_it == inner_info->field_index.end()) {
    error(expr.loc, "Result layout missing is_ok_/ok_val_/err_val_");
    return nullptr;
  }

  llvm::Value* res_ptr = classValuePtr(*expr.inner, inner_ty);
  if (!res_ptr) {
    return nullptr;
  }

  llvm::Value* is_ok_gep =
      builder_.CreateStructGEP(inner_info->type, res_ptr, is_ok_it->second, "try.is_ok.ptr");
  llvm::Value* is_ok =
      builder_.CreateLoad(llvmTypeForTypeName("bool"), is_ok_gep, "try.is_ok");
  is_ok = toI1(is_ok);

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* ok_bb = llvm::BasicBlock::Create(globalContext(), "try.ok", fn);
  llvm::BasicBlock* err_bb = llvm::BasicBlock::Create(globalContext(), "try.err", fn);
  builder_.CreateCondBr(is_ok, ok_bb, err_bb);

  // Error path: build enclosing Result(false, zero T, err), clean up, ret.
  builder_.SetInsertPoint(err_bb);
  llvm::AllocaInst* out = createEntryAlloca(fn, fn_info->type, "try.err.res");
  for (size_t i = 0; i < fn_info->field_types.size(); ++i) {
    llvm::Type* fty = llvmTypeForTypeName(fn_info->field_types[i]);
    llvm::Value* gep =
        builder_.CreateStructGEP(fn_info->type, out, static_cast<unsigned>(i), "try.err.z");
    builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
  }
  auto fn_is_ok = fn_info->field_index.find("is_ok_");
  auto fn_err = fn_info->field_index.find("err_val_");
  if (fn_is_ok == fn_info->field_index.end() || fn_err == fn_info->field_index.end()) {
    error(expr.loc, "enclosing Result layout missing fields");
    return nullptr;
  }
  builder_.CreateStore(
      llvm::ConstantInt::getFalse(globalContext()),
      builder_.CreateStructGEP(fn_info->type, out, fn_is_ok->second, "try.err.flag"));
  llvm::Value* err_src =
      builder_.CreateStructGEP(inner_info->type, res_ptr, err_it->second, "try.err.src");
  llvm::Type* err_ty = llvmTypeForTypeName(inner_info->field_types[err_it->second]);
  llvm::Value* err_val = builder_.CreateLoad(err_ty, err_src, "try.err.val");
  builder_.CreateStore(
      err_val, builder_.CreateStructGEP(fn_info->type, out, fn_err->second, "try.err.dst"));

  // Emit cleanups on the err path without clearing live scopes for the ok path.
  auto saved_defers = defer_scopes_;
  auto saved_autofree = auto_free_scopes_;
  auto saved_arena = arena_locals_;
  auto saved_held_sync = held_sync_locks_;
  if (!finishCleanups()) {
    return nullptr;
  }
  emitLifecycleDeinitCall();
  llvm::Value* ret_v = builder_.CreateLoad(fn_info->type, out, "try.err.ret");
  builder_.CreateRet(ret_v);
  defer_scopes_ = std::move(saved_defers);
  auto_free_scopes_ = std::move(saved_autofree);
  arena_locals_ = std::move(saved_arena);
  held_sync_locks_ = std::move(saved_held_sync);

  // Ok path: yield ok_val_.
  builder_.SetInsertPoint(ok_bb);
  llvm::Value* ok_gep =
      builder_.CreateStructGEP(inner_info->type, res_ptr, ok_it->second, "try.ok.ptr");
  const std::string& ok_ty_name = inner_info->field_types[ok_it->second];
  if (isClassType(ok_ty_name)) {
    return ok_gep;
  }
  return builder_.CreateLoad(llvmTypeForTypeName(ok_ty_name), ok_gep, "try.ok.val");
}

llvm::Value* Codegen::emitCast(const ast::CastExpr& expr) {
  if (!expr.value) {
    error(expr.loc, "cast missing value");
    return nullptr;
  }
  std::string from_ty = inferTypeFromExpr(*expr.value);
  if (from_ty.empty()) {
    from_ty = exprTypeOf(*expr.value);
  }
  const std::string& to_ty = expr.type_name;
  if (to_ty.empty()) {
    error(expr.loc, "cast missing target type");
    return nullptr;
  }
  if (!ensureTypeAvailable(to_ty) && !isInterfaceType(to_ty)) {
    error(expr.loc, "unknown cast target type: " + to_ty);
    return nullptr;
  }

  // Interface → class: compare vtable[0] TypeID, result is Optional<Class>.
  if (isInterfaceType(from_ty) && isClassType(to_ty)) {
    if (!ensureTypeAvailable(to_ty)) {
      error(expr.loc, "unknown class in cast: " + to_ty);
      return nullptr;
    }
    const std::string opt_ty = "Optional<" + to_ty + ">";
    if (!ensureTypeAvailable(opt_ty)) {
      error(expr.loc, "cannot specialize " + opt_ty);
      return nullptr;
    }
    const ClassInfo* opt_info = findClass(opt_ty);
    ClassInfo* cls_info = findClassMut(to_ty);
    const InterfaceInfo* iface = findInterface(from_ty);
    if (!opt_info || !cls_info || !iface) {
      return nullptr;
    }
    llvm::GlobalVariable* expect_tid = ensureTypeId(*cls_info);
    auto has_it = opt_info->field_index.find("has_value_");
    auto val_it = opt_info->field_index.find("value_");
    if (has_it == opt_info->field_index.end() || val_it == opt_info->field_index.end()) {
      error(expr.loc, "Optional layout missing has_value_/value_");
      return nullptr;
    }

    llvm::Value* fat = emitExpr(*expr.value);
    if (!fat) {
      return nullptr;
    }
    if (fat->getType()->isPointerTy()) {
      fat = builder_.CreateLoad(fatPtrTy(), fat, "cast.fat");
    }
    llvm::Value* data = builder_.CreateExtractValue(fat, 0, "cast.data");
    llvm::Value* vt = builder_.CreateExtractValue(fat, 1, "cast.vt");

    // Load TypeID from vtable slot 0 and icmp against the target class TypeID.
    llvm::ArrayType* arr_ty = vtableArrayTy(*iface);
    llvm::Value* tid_slot = builder_.CreateInBoundsGEP(
        arr_ty, vt,
        {llvm::ConstantInt::get(i32Ty(), 0), llvm::ConstantInt::get(i32Ty(), 0)},
        "cast.tid.slot");
    llvm::Value* got_tid = builder_.CreateLoad(ptrTy(), tid_slot, "cast.tid");
    llvm::Value* expect = builder_.CreateBitCast(expect_tid, ptrTy(), "cast.expect_tid");
    llvm::Value* ok = builder_.CreateICmpEQ(got_tid, expect, "cast.ok");

    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* out = createEntryAlloca(fn, opt_info->type, "cast.opt");
    for (size_t i = 0; i < opt_info->field_types.size(); ++i) {
      llvm::Type* fty = opt_info->type->getElementType(static_cast<unsigned>(i));
      llvm::Value* gep =
          builder_.CreateStructGEP(opt_info->type, out, static_cast<unsigned>(i), "cast.z");
      builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
    }
    llvm::Value* has_gep =
        builder_.CreateStructGEP(opt_info->type, out, has_it->second, "cast.has");
    builder_.CreateStore(ok, has_gep);

    llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(globalContext(), "cast.then", fn);
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(globalContext(), "cast.merge", fn);
    builder_.CreateCondBr(ok, then_bb, merge_bb);

    builder_.SetInsertPoint(then_bb);
    llvm::Value* val_gep =
        builder_.CreateStructGEP(opt_info->type, out, val_it->second, "cast.val");
    llvm::Value* obj = builder_.CreateLoad(cls_info->type, data, "cast.obj");
    builder_.CreateStore(obj, val_gep);
    builder_.CreateBr(merge_bb);

    builder_.SetInsertPoint(merge_bb);
    return out;
  }

  // Class → interface: auto-box into a fat pointer.
  if (isClassType(from_ty) && isInterfaceType(to_ty)) {
    llvm::Value* obj = nullptr;
    if (expr.value->kind() == ast::ExprKind::New) {
      obj = heapAllocNewExpr(static_cast<const ast::NewExpr&>(*expr.value));
    } else {
      obj = classValuePtr(*expr.value, from_ty);
    }
    if (!obj) {
      return nullptr;
    }
    return boxToInterface(obj, from_ty, to_ty, expr.loc);
  }

  // Opaque FFI: Class* → ptr (zero-cost bitcast of the object address).
  if (isClassType(from_ty) && to_ty == "ptr") {
    llvm::Value* p = classValuePtr(*expr.value, from_ty);
    if (!p) {
      return nullptr;
    }
    if (!p->getType()->isPointerTy()) {
      error(expr.loc, "class → ptr cast expects a pointer to the object");
      return nullptr;
    }
    return builder_.CreateBitCast(p, ptrTy(), "cast.class_to_ptr");
  }

  // Opaque FFI: ptr → Class* (borrowed handle; caller keeps ownership).
  if (from_ty == "ptr" && isClassType(to_ty)) {
    if (!ensureTypeAvailable(to_ty)) {
      error(expr.loc, "unknown class in cast: " + to_ty);
      return nullptr;
    }
    llvm::Value* p = emitExpr(*expr.value);
    if (!p) {
      return nullptr;
    }
    if (!p->getType()->isPointerTy()) {
      error(expr.loc, "ptr → class cast expects a pointer value");
      return nullptr;
    }
    return builder_.CreateBitCast(p, ptrTy(), "cast.ptr_to_class");
  }

  // Concrete class → class: pointer bitcast (no inheritance yet).
  if (isClassType(from_ty) && isClassType(to_ty)) {
    llvm::Value* p = classValuePtr(*expr.value, from_ty);
    if (!p) {
      return nullptr;
    }
    return builder_.CreateBitCast(p, ptrTy(), "cast.ptr");
  }

  if (from_ty == to_ty) {
    return emitExpr(*expr.value);
  }

  // Primitive / trivial casts.
  llvm::Value* v = emitExpr(*expr.value);
  if (!v) {
    return nullptr;
  }
  if (to_ty == "bool") {
    return toI1(v);
  }
  if (to_ty == "i32") {
    return toI32(v, expr.loc);
  }
  error(expr.loc, "unsupported cast from '" + from_ty + "' to '" + to_ty + "'");
  return nullptr;
}

std::string Codegen::inferTypeFromExpr(const ast::Expr& expr) {
  // Bare `"text"` in `:=` contexts is a String when the stdlib type is loaded.
  // [A005] Never fall back to `ptr`: a missing stdlib/string.l would otherwise
  // degrade silently and surface later as "internal: cannot convert a
  // non-integer value to i32" from the integer print path. Report the real
  // cause instead; callers treat an empty result as "cannot infer".
  if (expr.kind() == ast::ExprKind::StringLiteral) {
    if (ensureTypeAvailable("String") && isClassType("String")) {
      return "String";
    }
    error(expr.loc,
          "string literal requires stdlib/string.l to be compiled in (add it to the source list)");
    return {};
  }
  if (expr.kind() == ast::ExprKind::FloatLiteral) {
    return "f64";
  }
  if (expr.kind() == ast::ExprKind::New) {
    const auto& neu = static_cast<const ast::NewExpr&>(expr);
    if (!ensureTypeAvailable(neu.class_name)) {
      return {};
    }
    return neu.class_name;
  }
  if (expr.kind() == ast::ExprKind::Heap) {
    const auto& heap = static_cast<const ast::HeapExpr&>(expr);
    if (heap.inner) {
      return inferTypeFromExpr(*heap.inner);
    }
    return {};
  }
  if (expr.kind() == ast::ExprKind::Call) {
    // [dsl] A block call's value is the root it built, not the helper's
    // declared return type — `cfg := app(type: App) { ... }` is an `App`.
    const auto& call = static_cast<const ast::CallExpr&>(expr);
    if (!call.config_type.empty() && !call.config_bind_target.empty()) {
      ensureTypeAvailable(call.config_type);
      return call.config_type;
    }
  }
  if (expr.kind() == ast::ExprKind::MethodCall ||
      expr.kind() == ast::ExprKind::Call ||
      expr.kind() == ast::ExprKind::ScopedCall ||
      expr.kind() == ast::ExprKind::Index ||
      expr.kind() == ast::ExprKind::MemberAccess) {
    // These carry a precise static type (see exprTypeOf). The generic fallback
    // below reports i32 for anything unknown, which would make e.g. a `ptr`
    // returning method look like an integer at cast / declaration sites.
    std::string t = exprTypeOf(expr);
    if (!t.empty()) {
      return t;
    }
    return {};
  }
  if (expr.kind() == ast::ExprKind::ArrayLiteral) {
    const auto& lit = static_cast<const ast::ArrayLiteralExpr&>(expr);
    std::string elem = "i32";
    if (!lit.elements.empty() && lit.elements[0]) {
      if (lit.elements[0]->kind() == ast::ExprKind::BoolLiteral) {
        elem = "bool";
      } else if (lit.elements[0]->kind() == ast::ExprKind::StringLiteral) {
        elem = "String";
      } else {
        std::string t = inferTypeFromExpr(*lit.elements[0]);
        if (!t.empty() && t != "i32") {
          // Keep primitives; nested Array<> etc. allowed as element type name.
          elem = t;
        } else if (lit.elements[0]->kind() == ast::ExprKind::IntLiteral) {
          elem = "i32";
        } else if (!t.empty()) {
          elem = t;
        }
      }
    }
    const std::string arr_ty = "Array<" + elem + ">";
    if (!ensureTypeAvailable(arr_ty)) {
      return {};
    }
    return arr_ty;
  }
  if (expr.kind() == ast::ExprKind::AddrOf) {
    return "ptr";
  }
  if (expr.kind() == ast::ExprKind::Cast) {
    const auto& cast = static_cast<const ast::CastExpr&>(expr);
    if (!cast.value) {
      return {};
    }
    std::string from_ty = inferTypeFromExpr(*cast.value);
    if (from_ty.empty()) {
      from_ty = exprTypeOf(*cast.value);
    }
    if (isInterfaceType(from_ty) && isClassType(cast.type_name)) {
      const std::string opt_ty = "Optional<" + cast.type_name + ">";
      if (!ensureTypeAvailable(opt_ty)) {
        return {};
      }
      return opt_ty;
    }
    if (!ensureTypeAvailable(cast.type_name)) {
      return {};
    }
    return cast.type_name;
  }
  if (expr.kind() == ast::ExprKind::IncDec) {
    return "i32";
  }
  if (expr.kind() == ast::ExprKind::Try) {
    const auto& try_expr = static_cast<const ast::TryExpr&>(expr);
    if (!try_expr.inner) {
      return {};
    }
    std::string inner_ty = inferTypeFromExpr(*try_expr.inner);
    if (inner_ty.empty()) {
      inner_ty = exprTypeOf(*try_expr.inner);
    }
    std::string base;
    std::vector<std::string> args;
    if (!splitGenericType(inner_ty, base, args) || base != "Result" || args.size() != 2) {
      return {};
    }
    return args[0];
  }
  // Phase 1: untyped `|x| ...` → fun(i32,...,i32)->i32 (no captures).
  if (expr.kind() == ast::ExprKind::Lambda) {
    const auto& lam = static_cast<const ast::LambdaExpr&>(expr);
    std::string out = "fun(";
    for (size_t i = 0; i < lam.params.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      out += "i32";
    }
    out += ")->i32";
    return out;
  }
  std::string ty = exprTypeOf(expr);
  if (!ty.empty()) {
    if (isClassType(ty) || ty == "String") {
      ensureTypeAvailable(ty);
    }
    return ty;
  }
  // Fall back: numeric-ish expressions stay i32 (existing := behavior).
  return "i32";
}

llvm::Constant* Codegen::evalConstInit(const ast::Expr& expr, llvm::Type* expected_ty) {
  switch (expr.kind()) {
    case ast::ExprKind::IntLiteral: {
      const auto& lit = static_cast<const ast::IntLiteralExpr&>(expr);
      long long n = 0;
      try {
        n = std::stoll(lit.value);
      } catch (...) {
        error(expr.loc, "invalid integer literal in global initializer");
        return nullptr;
      }
      llvm::Type* ty = expected_ty ? expected_ty : i32Ty();
      if (!ty->isIntegerTy()) {
        error(expr.loc, "integer literal cannot initialize non-integer global");
        return nullptr;
      }
      return llvm::ConstantInt::get(ty, static_cast<uint64_t>(n), /*isSigned=*/true);
    }
    case ast::ExprKind::BoolLiteral: {
      const auto& lit = static_cast<const ast::BooleanLiteralExpr&>(expr);
      llvm::Type* ty = expected_ty ? expected_ty : i1Ty();
      if (!ty->isIntegerTy(1)) {
        error(expr.loc, "bool literal cannot initialize non-bool global");
        return nullptr;
      }
      return lit.value ? llvm::ConstantInt::getTrue(globalContext())
                       : llvm::ConstantInt::getFalse(globalContext());
    }
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      auto cit = const_values_.find(id.name);
      if (cit != const_values_.end()) {
        return cit->second;
      }
      auto git = global_vars_.find(id.name);
      if (git != global_vars_.end() && git->second->isConstant()) {
        if (auto* c = llvm::dyn_cast<llvm::Constant>(git->second->getInitializer())) {
          return c;
        }
      }
      error(expr.loc, "global initializer must be a compile-time constant");
      return nullptr;
    }
    default:
      error(expr.loc, "unsupported global initializer (Phase 1: literals / const only)");
      return nullptr;
  }
}

llvm::Value* Codegen::lookupStorage(const std::string& name) {
  auto lit = named_values_.find(name);
  if (lit != named_values_.end()) {
    return lit->second;
  }
  auto git = global_vars_.find(name);
  if (git != global_vars_.end()) {
    return git->second;
  }
  return nullptr;
}

// Module-level `const NAME = <literal>`: inlined at every use site, no storage.
//
// Constants are module-scoped. `emit()` is driven by the primary module's
// statements only, and imported modules' bodies are inlined into this module's
// AST, so an imported `const` (e.g. `KIND_POS` in apps/cad/entity.l) would
// otherwise be invisible to the very method bodies that reference it.
void Codegen::registerModuleConst(const ast::VarDeclStmt& decl) {
  if (const_values_.count(decl.name) != 0) {
    return;  // already registered (primary module pass reached it first)
  }
  if (!decl.init) {
    error(decl.loc, "const '" + decl.name + "' requires an initializer");
    return;
  }
  std::string type_name = decl.type_name;
  if (type_name.empty()) {
    if (decl.init->kind() == ast::ExprKind::BoolLiteral) {
      type_name = "bool";
    } else {
      type_name = inferTypeFromExpr(*decl.init);
      if (type_name.empty()) {
        type_name = "i32";
      }
    }
  }
  llvm::Constant* init = evalConstInit(*decl.init, llvmTypeForTypeName(type_name));
  if (!init) {
    return;
  }
  const_values_[decl.name] = init;
  const_types_[decl.name] = type_name;
}

bool Codegen::emitGlobalVarDecl(const ast::VarDeclStmt& decl) {
  if (const_values_.count(decl.name) != 0 || global_vars_.count(decl.name) != 0 ||
      named_values_.count(decl.name) != 0) {
    error(decl.loc, "redefinition of global '" + decl.name + "'");
    return false;
  }
  if (!decl.init) {
    error(decl.loc, "global '" + decl.name + "' requires an initializer");
    return false;
  }

  std::string type_name = decl.type_name;
  if (type_name.empty()) {
    type_name = inferTypeFromExpr(*decl.init);
    if (type_name.empty()) {
      // Literals without class context default to i32 / bool.
      if (decl.init->kind() == ast::ExprKind::BoolLiteral) {
        type_name = "bool";
      } else {
        type_name = "i32";
      }
    }
  }
  if (isInterfaceType(type_name)) {
    error(decl.loc, "global interface instances are not supported ('" + decl.name + "')");
    return false;
  }

  // Class globals: zero-init storage now; run Class(...) / init in __l_global_init.
  if (isClassType(type_name)) {
    if (decl.decl_kind == ast::VarDeclKind::Const) {
      error(decl.loc, "const global class instances are not supported ('" + decl.name + "')");
      return false;
    }
    if (decl.init->kind() != ast::ExprKind::New) {
      error(decl.loc, "global class '" + decl.name + "' must be initialized with ClassName(...)");
      return false;
    }
    const auto& neu = static_cast<const ast::NewExpr&>(*decl.init);
    if (neu.class_name != type_name) {
      error(decl.loc, "global '" + decl.name + "' type '" + type_name +
                          "' does not match initializer '" + neu.class_name + "'");
      return false;
    }
    if (!ensureTypeAvailable(type_name)) {
      error(decl.loc, "unknown class type for global '" + decl.name + "'");
      return false;
    }
    const ClassInfo* info = findClass(type_name);
    if (!info || !info->type) {
      error(decl.loc, "unknown class type for global '" + decl.name + "'");
      return false;
    }
    llvm::Constant* zero = llvm::ConstantAggregateZero::get(info->type);
    // Explicit typed globals are mutable; `:=` / constd stay readonly.
    const bool is_readonly = decl.decl_kind == ast::VarDeclKind::Constd ||
                             (decl.decl_kind == ast::VarDeclKind::Infer && !decl.is_mutable);
    auto* gv = new llvm::GlobalVariable(*module_, info->type, /*isConstant=*/is_readonly,
                                        llvm::GlobalValue::InternalLinkage, zero, decl.name);
    global_vars_[decl.name] = gv;
    global_types_[decl.name] = type_name;
    if (is_readonly) {
      global_readonly_.insert(decl.name);
    }
    PendingGlobalClassInit pending;
    pending.name = decl.name;
    pending.class_name = type_name;
    pending.neu = &neu;
    pending_global_class_inits_.push_back(pending);
    return true;
  }

  llvm::Type* lty = llvmTypeForTypeName(type_name);
  llvm::Constant* init = evalConstInit(*decl.init, lty);
  if (!init) {
    return false;
  }

  if (decl.decl_kind == ast::VarDeclKind::Const) {
    const_values_[decl.name] = init;
    const_types_[decl.name] = type_name;
    return true;
  }

  const bool is_readonly = decl.decl_kind == ast::VarDeclKind::Constd ||
                           (decl.decl_kind == ast::VarDeclKind::Infer && !decl.is_mutable);
  auto* gv = new llvm::GlobalVariable(
      *module_, lty, /*isConstant=*/is_readonly, llvm::GlobalValue::InternalLinkage, init,
      decl.name);
  global_vars_[decl.name] = gv;
  global_types_[decl.name] = type_name;
  if (is_readonly) {
    global_readonly_.insert(decl.name);
  }
  return true;
}

bool Codegen::emitDestructureDecl(const ast::DestructureDeclStmt& decl) {
  if (!decl.init) {
    error(decl.loc, "destructure requires an initializer");
    return false;
  }
  if (decl.bindings.size() < 2) {
    error(decl.loc, "destructure requires at least two bindings");
    return false;
  }

  const std::string type_name = inferTypeFromExpr(*decl.init);
  if (type_name.empty() || !isClassType(type_name)) {
    error(decl.loc, "destructure RHS must be a class/struct value");
    return false;
  }
  if (!ensureTypeAvailable(type_name)) {
    error(decl.loc, "unknown class in destructure: " + type_name);
    return false;
  }
  const ClassInfo* info = findClass(type_name);
  if (!info) {
    error(decl.loc, "unknown class in destructure: " + type_name);
    return false;
  }
  if (decl.bindings.size() != info->field_types.size()) {
    error(decl.loc, "destructure binding count (" + std::to_string(decl.bindings.size()) +
                        ") does not match field count of '" + type_name + "' (" +
                        std::to_string(info->field_types.size()) + ")");
    return false;
  }

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::Value* base = classValuePtr(*decl.init, type_name);
  if (!base) {
    return false;
  }

  for (size_t i = 0; i < decl.bindings.size(); ++i) {
    const ast::DestructureBinding& b = decl.bindings[i];
    const std::string& fty_name = info->field_types[i];
    llvm::Value* gep =
        builder_.CreateStructGEP(info->type, base, static_cast<unsigned>(i), b.name + ".src");

    if (isClassType(fty_name)) {
      const ClassInfo* finfo = findClass(fty_name);
      if (!finfo) {
        error(decl.loc, "unknown field class type '" + fty_name + "'");
        return false;
      }
      llvm::AllocaInst* slot = createEntryAlloca(fn, finfo->type, b.name);
      llvm::Value* loaded = builder_.CreateLoad(finfo->type, gep, b.name + ".copy");
      builder_.CreateStore(loaded, slot);
      named_values_[b.name] = slot;
      local_types_[b.name] = fty_name;
      heap_locals_.erase(b.name);
      continue;
    }

    if (isInterfaceType(fty_name)) {
      llvm::AllocaInst* slot = createEntryAlloca(fn, fatPtrTy(), b.name);
      llvm::Value* loaded = builder_.CreateLoad(fatPtrTy(), gep, b.name + ".fat");
      builder_.CreateStore(loaded, slot);
      named_values_[b.name] = slot;
      local_types_[b.name] = fty_name;
      continue;
    }

    llvm::Type* lty = llvmTypeForTypeName(fty_name);
    llvm::AllocaInst* slot = createEntryAlloca(fn, lty, b.name);
    llvm::Value* loaded = builder_.CreateLoad(lty, gep, b.name + ".val");
    builder_.CreateStore(loaded, slot);
    named_values_[b.name] = slot;
    local_types_[b.name] = fty_name;
  }
  return true;
}

bool Codegen::emitVarDecl(const ast::VarDeclStmt& decl) {
  // Nested const/constd inside a function: still allocate local storage (or inline const).
  if (decl.decl_kind == ast::VarDeclKind::Const) {
    if (!decl.init) {
      error(decl.loc, "const '" + decl.name + "' requires an initializer");
      return false;
    }
    std::string type_name = decl.type_name.empty() ? "i32" : decl.type_name;
    if (decl.type_name.empty()) {
      if (decl.init->kind() == ast::ExprKind::BoolLiteral) {
        type_name = "bool";
      }
    }
    llvm::Constant* init = evalConstInit(*decl.init, llvmTypeForTypeName(type_name));
    if (!init) {
      return false;
    }
    const_values_[decl.name] = init;
    const_types_[decl.name] = type_name;
    local_types_[decl.name] = type_name;
    return true;
  }

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();

  std::string type_name = decl.type_name;
  if (type_name.empty()) {
    if (!decl.init) {
      error(decl.loc, "inferred declaration requires an initializer: " + decl.name);
      return false;
    }
    type_name = inferTypeFromExpr(*decl.init);
    if (type_name.empty()) {
      error(decl.loc, "cannot infer type for '" + decl.name + "' from initializer");
      return false;
    }
  }

  if (!type_name.empty()) {
    if (!ensureTypeAvailable(type_name)) {
      return false;
    }
  }

  // Explicit primitive declarations (`i32 p = pkg::port()`) are known non-class,
  // non-interface values — but the initializer may still be a namespaced call.
  const bool is_scalar =
      type_name == "i32" || type_name == "i64" || type_name == "f64" || type_name == "bool";
    if (is_scalar && decl.init && decl.init->kind() == ast::ExprKind::ScopedCall) {
      llvm::Value* src = emitExpr(*decl.init);
      if (!src) {
        return false;
      }
      llvm::Type* rhs_ty = llvmTypeForTypeName(type_name);
      llvm::Value* scalar_slot = createLocalStorage(fn, rhs_ty, decl.name);
      builder_.CreateStore(coerceScalarTo(src, rhs_ty), scalar_slot);
      named_values_[decl.name] = scalar_slot;
      local_types_[decl.name] = type_name;
      return true;
    }

  if (type_name == "Arena") {
    llvm::Value* slot = createLocalStorage(fn, ptrTy(), decl.name);
    if (!decl.init) {
      error(decl.loc, "Arena '" + decl.name + "' requires an initializer");
      return false;
    }
    llvm::Value* arena_ptr = emitExpr(*decl.init);
    if (!arena_ptr) {
      return false;
    }
    builder_.CreateStore(arena_ptr, slot);
    named_values_[decl.name] = slot;
    local_types_[decl.name] = "Arena";
    arena_locals_.insert(decl.name);
    noteAutoFreeLocal(decl.name);  // scope exit → l_arena_free
    return true;
  }

  if (isClassType(type_name)) {
    const ClassInfo* info = findClass(type_name);
    if (!info) {
      error(decl.loc, "unknown class in declaration: " + type_name);
      return false;
    }
    local_types_[decl.name] = type_name;

    // 1. Stack value copied in place: `Cfg c = pkg::make(x)` / `y := pkg::mk()`.
    if (decl.init && decl.init->kind() == ast::ExprKind::ScopedCall) {
      llvm::AllocaInst* dst = createEntryAlloca(fn, info->type, decl.name);
      named_values_[decl.name] = dst;
      llvm::Value* src = emitExpr(*decl.init);
      if (!src || !src->getType()->isPointerTy()) {
        error(decl.loc, "namespaced constructor for '" + decl.name + "' must yield a class value");
        return false;
      }
      if (!initStackClassFromPtr(dst, *info, src, decl.name + ".copy")) {
        return false;
      }
      return true;
    }

    // 2. `drop()` classes: must live on the heap so auto-free can run drop().
    const bool needs_drop = info->methods.count("drop") != 0;
    if (needs_drop && decl.init && decl.init->kind() == ast::ExprKind::New) {
      const auto& neu = static_cast<const ast::NewExpr&>(*decl.init);
      if (neu.class_name != type_name) {
        error(decl.loc, "class type mismatch in initialization of " + decl.name);
        return false;
      }
      llvm::Value* raw = emitHeapClassAllocAndInit(*info, neu, decl.name);
      if (!raw) {
        return false;
      }
      named_values_[decl.name] = createLocalStorage(fn, ptrTy(), decl.name);
      builder_.CreateStore(raw, named_values_[decl.name]);
      heap_locals_.insert(decl.name);
      noteAutoFreeLocal(decl.name);
      return true;
    }

    // 3. `heap Class(...)` — owning class pointer.
    if (decl.init && decl.init->kind() == ast::ExprKind::Heap) {
      const auto& heap = static_cast<const ast::HeapExpr&>(*decl.init);
      if (!heap.inner || heap.inner->kind() != ast::ExprKind::New) {
        error(decl.loc, "heap init requires ClassName(...)");
        return false;
      }
      const auto& neu = static_cast<const ast::NewExpr&>(*heap.inner);
      if (neu.class_name != type_name) {
        error(decl.loc, "class type mismatch in heap initialization of " + decl.name);
        return false;
      }
      llvm::Value* heap_slot = createLocalStorage(fn, ptrTy(), decl.name);
      llvm::Value* heap_ptr = emitExpr(*decl.init);
      if (!heap_ptr) {
        return false;
      }
      builder_.CreateStore(heap_ptr, heap_slot);
      named_values_[decl.name] = heap_slot;
      heap_locals_.insert(decl.name);
      noteAutoFreeLocal(decl.name);
      return true;
    }

    // 4. Arena bump-owned class* — never individually l_free'd.
    if (decl.init && isArenaOwnedClassExpr(*decl.init)) {
      llvm::Value* arena_obj_slot = createLocalStorage(fn, ptrTy(), decl.name);
      llvm::Value* ptr = emitExpr(*decl.init);
      if (!ptr) {
        return false;
      }
      if (!ptr->getType()->isPointerTy()) {
        error(decl.loc, "arena-owned init for '" + decl.name + "' expected a pointer");
        return false;
      }
      builder_.CreateStore(ptr, arena_obj_slot);
      named_values_[decl.name] = arena_obj_slot;
      heap_locals_.insert(decl.name);
      return true;
    }

    // 5. Owning heap class from an expression (`s := a + b`).
    if (decl.init && isOwnedHeapClassExpr(*decl.init)) {
      llvm::Value* owned_slot = createLocalStorage(fn, ptrTy(), decl.name);
      llvm::Value* heap_ptr = emitExpr(*decl.init);
      if (!heap_ptr) {
        return false;
      }
      if (!heap_ptr->getType()->isPointerTy()) {
        error(decl.loc, "owned heap init for '" + decl.name + "' expected a pointer");
        return false;
      }
      builder_.CreateStore(heap_ptr, owned_slot);
      named_values_[decl.name] = owned_slot;
      heap_locals_.insert(decl.name);
      if (type_name == "String") {
        owned_string_locals_.insert(decl.name);
      }
      noteAutoFreeLocal(decl.name);
      return true;
    }

    // 6. `c := opaque_ptr as Counter` — borrowed Class* handle (no auto-free).
    if (decl.init && decl.init->kind() == ast::ExprKind::Cast) {
      const auto& cast = static_cast<const ast::CastExpr&>(*decl.init);
      if (cast.value && isClassType(cast.type_name) && cast.type_name == type_name) {
        std::string from_ty = inferTypeFromExpr(*cast.value);
        if (from_ty.empty()) {
          from_ty = exprTypeOf(*cast.value);
        }
        if (from_ty == "ptr") {
          llvm::Value* slot = createLocalStorage(fn, ptrTy(), decl.name);
          llvm::Value* obj = emitExpr(*decl.init);
          if (!obj) {
            return false;
          }
          if (!obj->getType()->isPointerTy()) {
            error(decl.loc, "ptr → class cast for '" + decl.name + "' expected a pointer");
            return false;
          }
          builder_.CreateStore(obj, slot);
          named_values_[decl.name] = slot;
          heap_locals_.insert(decl.name);
          return true;
        }
      }
    }

    // 7. Stack class value built in place.
    llvm::AllocaInst* class_value_slot = createEntryAlloca(fn, info->type, decl.name);
    named_values_[decl.name] = class_value_slot;
    heap_locals_.erase(decl.name);
    if (!decl.init) {
      return true;
    }
    if (!emitStackClassInit(class_value_slot, *info, decl)) {
      return false;
    }
    // A stack Array owns its element buffer (and, for `Array<IFace>`, the
    // heap-escaped objects behind each fat pointer). Scope exit runs
    // emitArrayOwnedCleanup, which frees the buffer and every interface element.
    if (info->template_name == "Array") {
      noteAutoFreeLocal(decl.name);
    }
    return true;
  }

  if (isInterfaceType(type_name)) {
    llvm::Value* alloca = createLocalStorage(fn, fatPtrTy(), decl.name);
    named_values_[decl.name] = alloca;
    local_types_[decl.name] = type_name;

    if (!decl.init) {
      error(decl.loc, "interface variable requires initializer");
      return false;
    }

    // Already an interface value: `r := p as IReader` or copy from another iface local.
    if (decl.init->kind() == ast::ExprKind::Cast) {
      const auto& cast = static_cast<const ast::CastExpr&>(*decl.init);
      if (isInterfaceType(cast.type_name)) {
        llvm::Value* fat = emitCast(cast);
        if (!fat) {
          return false;
        }
        builder_.CreateStore(fat, alloca);
        return true;
      }
    }
    if (decl.init->kind() == ast::ExprKind::Identifier) {
      const auto& id = static_cast<const ast::IdentifierExpr&>(*decl.init);
      auto lit = local_types_.find(id.name);
      if (lit != local_types_.end() && isInterfaceType(lit->second)) {
        llvm::Value* fat = emitExpr(*decl.init);
        if (!fat) {
          return false;
        }
        builder_.CreateStore(fat, alloca);
        return true;
      }
    }

    // Already an interface value produced by an expression, e.g.
    // `IPlugin p = arr.get(i)` or `p := arr.get(i)`. The call yields the fat
    // pointer directly, so it can be stored without any boxing.
    if (decl.init->kind() == ast::ExprKind::Call ||
        decl.init->kind() == ast::ExprKind::MethodCall ||
        decl.init->kind() == ast::ExprKind::Index ||
        decl.init->kind() == ast::ExprKind::MemberAccess) {
      llvm::Value* fat = emitExpr(*decl.init);
      if (!fat) {
        return false;
      }
      if (fat->getType() == fatPtrTy()) {
        builder_.CreateStore(fat, alloca);
        return true;
      }
      // Not a fat pointer after all — fall through to class boxing.
    }

    // Auto-box: IReader iface = p;  or  IReader r = Point(42);
    // NewExpr is heap-escaped so the fat pointer may outlive the statement (e.g. Array).
    std::string class_ty;
    llvm::Value* obj = nullptr;
    if (decl.init->kind() == ast::ExprKind::Identifier) {
      const auto& id = static_cast<const ast::IdentifierExpr&>(*decl.init);
      auto lit = local_types_.find(id.name);
      if (lit == local_types_.end() || !isClassType(lit->second)) {
        error(decl.loc, "interface init expects a class instance");
        return false;
      }
      class_ty = lit->second;
      obj = classObjectPtr(id.name, decl.loc);
    } else if (decl.init->kind() == ast::ExprKind::New) {
      const auto& neu = static_cast<const ast::NewExpr&>(*decl.init);
      class_ty = neu.class_name;
      obj = heapAllocNewExpr(neu);
    } else {
      class_ty = inferTypeFromExpr(*decl.init);
      if (!isClassType(class_ty)) {
        error(decl.loc, "interface init expects a class instance");
        return false;
      }
      obj = classValuePtr(*decl.init, class_ty);
    }
    if (!obj) {
      return false;
    }
    llvm::Value* fat = boxToInterface(obj, class_ty, type_name, decl.loc);
    if (!fat) {
      return false;
    }
    builder_.CreateStore(fat, alloca);
    return true;
  }

  // First-class function local: `double := |x| x * 2` / `fun(i32)->i32 f = ...`
  // Escaped returns: `cb := get_closure()` stores fat `{fn,ctx}` ([link:75-Escape-Analysis-Auto-Heap-Promotion]).
  if (isFunTypeName(type_name)) {
    if (!decl.init) {
      error(decl.loc, "function variable '" + decl.name + "' requires an initializer");
      return false;
    }

    // Detect call to a function that returns an escaped fat closure.
    bool from_fat_return = false;
    std::string payload_ty = type_name;
    if (decl.init->kind() == ast::ExprKind::Call) {
      const auto& call = static_cast<const ast::CallExpr&>(*decl.init);
      auto fit = concrete_funcs_.find(call.callee);
      if (fit != concrete_funcs_.end() && fit->second && fit->second->returns_fat_closure) {
        from_fat_return = true;
        // Prefer the payload fun type inferred from the callee's declared return,
        // or keep := inferred type_name (often fun()->i32 from use sites).
        if (!fit->second->return_type.empty() && isFunTypeName(fit->second->return_type)) {
          // Declared `-> fun()` is void-payload; escaping expr lambdas are fun()->i32.
          if (fit->second->return_type == "fun()" || fit->second->return_type == "fun") {
            payload_ty = "fun()->i32";
          } else {
            payload_ty = resolveTypeName(fit->second->return_type);
          }
        }
      }
    }

    if (from_fat_return) {
      llvm::Value* alloca = createLocalStorage(fn, fatClosureTy(), decl.name);
      named_values_[decl.name] = alloca;
      local_types_[decl.name] = payload_ty;
      llvm::Value* init = emitExpr(*decl.init);
      if (!init) {
        return false;
      }
      if (init->getType()->isPointerTy()) {
        init = builder_.CreateLoad(fatClosureTy(), init, decl.name + ".fat.ld");
      }
      builder_.CreateStore(init, alloca);
      fat_closure_locals_.insert(decl.name);
      // Escaping ctx embeds capture values; scope exit frees the single heap ctx.
      fat_closure_ctx_tys_[decl.name] = nullptr;
      noteAutoFreeLocal(decl.name);
      return true;
    }

    llvm::Value* alloca = createLocalStorage(fn, ptrTy(), decl.name);
    named_values_[decl.name] = alloca;
    local_types_[decl.name] = type_name;
    pending_closure_ctx_ = nullptr;
    pending_closure_ctx_ty_ = nullptr;
    llvm::Value* init = nullptr;
    if (decl.init->kind() == ast::ExprKind::Lambda) {
      init = emitLambda(static_cast<const ast::LambdaExpr&>(*decl.init), type_name);
    } else {
      init = emitExpr(*decl.init);
    }
    if (!init) {
      return false;
    }
    if (pending_closure_ctx_) {
      ClosureLocal cl;
      cl.fn = llvm::dyn_cast<llvm::Function>(init);
      cl.ctx = pending_closure_ctx_;
      cl.ctx_ty = pending_closure_ctx_ty_;
      if (!cl.fn) {
        error(decl.loc, "capturing lambda did not produce a function");
        return false;
      }
      closure_locals_[decl.name] = cl;
      pending_closure_ctx_ = nullptr;
      pending_closure_ctx_ty_ = nullptr;
    }
    builder_.CreateStore(init, alloca);
    return true;
  }

  llvm::Type* lty = llvmTypeForTypeName(type_name);
  // `ptr p = <heapLocal> as ptr` keeps a raw alias of the owning heap local.
  if (type_name == "ptr" && decl.init && decl.init->kind() == ast::ExprKind::Cast) {
    const auto& cast = static_cast<const ast::CastExpr&>(*decl.init);
    if (cast.value && cast.value->kind() == ast::ExprKind::Identifier) {
      const auto& src = static_cast<const ast::IdentifierExpr&>(*cast.value);
      if (heap_locals_.count(src.name) != 0) {
        ptr_aliases_[decl.name] = src.name;
      }
    }
  }
  llvm::Value* alloca = createLocalStorage(fn, lty, decl.name);
  llvm::Value* init = nullptr;
  if (decl.init) {
    // [link:50-Variant] `n := <Variant expr>` with an inferred primitive/String target.
    init = emitContextualVariantUnbox(*decl.init, type_name, lty, /*immediate=*/true);
    if (!init) {
      init = emitExpr(*decl.init);
    }
    if (!init) {
      return false;
    }
    if (lty->isIntegerTy(32) && !init->getType()->isPointerTy()) {
      init = toI32(init, decl.loc);
    } else if (lty->isIntegerTy(64) && init->getType()->isIntegerTy()) {
      init = builder_.CreateIntCast(init, i64Ty(), /*isSigned=*/true, "init.i64");
    } else if (lty->isDoubleTy()) {
      if (init->getType()->isIntegerTy()) {
        init = builder_.CreateSIToFP(init, f64Ty(), "init.f64");
      } else if (!init->getType()->isDoubleTy()) {
        error(decl.loc, "cannot initialize f64 '" + decl.name + "' from non-numeric expression");
        return false;
      }
    }
  } else {
    init = llvm::Constant::getNullValue(lty);
  }
  builder_.CreateStore(init, alloca);
  named_values_[decl.name] = alloca;
  local_types_[decl.name] = type_name;
  return true;
}

// `print(v)` where v:Variant — dispatch on `type_tag_` and print the payload.
// Each arm converts its payload to a C string, prints it with `l_print_str`,
// then jumps to the shared exit block (which prints the newline).
llvm::Value* Codegen::emitPrintVariant(const ast::Expr& arg_expr, llvm::Value* var_val,
                                       const ast::SourceLocation& loc) {
  (void)arg_expr;
  const ClassInfo* vinfo = findClass("Variant");
  const ClassInfo* sinfo = findClass("String");
  if (!vinfo || !vinfo->type || !sinfo || !sinfo->type) {
    error(loc, "print(Variant) requires Variant and String types");
    return nullptr;
  }
  // A class value may arrive as a pointer to its storage; normalize to a value.
  if (var_val->getType()->isPointerTy()) {
    var_val = builder_.CreateLoad(vinfo->type, var_val, "pv.variant");
  }
  llvm::Value* tag = builder_.CreateExtractValue(var_val, 0, "pv.tag");
  llvm::Value* data = builder_.CreateExtractValue(var_val, 1, "pv.data");
  llvm::Function* print_str = requireExtern("l_print_str", loc);
  llvm::Function* int_str = requireExtern("l_int_to_str", loc);
  llvm::Function* f64_str = requireExtern("l_f64_to_str", loc);
  llvm::Function* load_i32 = requireExtern("l_load_i32", loc);
  llvm::Function* load_f64 = requireExtern("l_load_f64", loc);
  if (!print_str || !int_str || !f64_str || !load_i32 || !load_f64) {
    error(loc, "print(Variant) requires runtime helpers");
    return nullptr;
  }

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* i32_bb = llvm::BasicBlock::Create(globalContext(), "pv.i32", fn);
  llvm::BasicBlock* str_bb = llvm::BasicBlock::Create(globalContext(), "pv.str", fn);
  llvm::BasicBlock* f64_bb = llvm::BasicBlock::Create(globalContext(), "pv.f64", fn);
  llvm::BasicBlock* bool_bb = llvm::BasicBlock::Create(globalContext(), "pv.bool", fn);
  llvm::BasicBlock* bad_bb = llvm::BasicBlock::Create(globalContext(), "pv.bad", fn);
  llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(globalContext(), "pv.merge", fn);

  auto is_tag = [&](int want) {
    return builder_.CreateICmpEQ(tag, llvm::ConstantInt::get(i32Ty(), want), "pv.is");
  };
  auto cell = [&]() { return builder_.CreateBitCast(data, ptrTy(), "pv.cell"); };

  // tag 1 → i32 cell
  builder_.CreateCondBr(is_tag(1), i32_bb, str_bb);
  builder_.SetInsertPoint(i32_bb);
  llvm::Value* i32_text = nullptr;
  {
    llvm::Value* num = builder_.CreateCall(load_i32, {cell()}, "pv.i32.val");
    i32_text = builder_.CreateCall(int_str, {num}, "pv.i32.text");
    builder_.CreateBr(merge_bb);
  }
  llvm::BasicBlock* i32_end = builder_.GetInsertBlock();

  // tag 2 → String payload: data_ points at the String object.
  builder_.SetInsertPoint(str_bb);
  llvm::BasicBlock* str_body = llvm::BasicBlock::Create(globalContext(), "pv.str.body", fn);
  builder_.CreateCondBr(is_tag(2), str_body, f64_bb);
  builder_.SetInsertPoint(str_body);
  llvm::Value* str_text = nullptr;
  {
    auto fit = sinfo->field_index.find("data_");
    str_text = data;
    if (fit != sinfo->field_index.end()) {
      llvm::Value* gep =
          builder_.CreateStructGEP(sinfo->type, cell(), fit->second, "pv.str.data");
      str_text = builder_.CreateLoad(ptrTy(), gep, "pv.str.ptr");
    }
    builder_.CreateBr(merge_bb);
  }
  llvm::BasicBlock* str_end = builder_.GetInsertBlock();

  // tag 3 → f64 cell
  builder_.SetInsertPoint(f64_bb);
  llvm::BasicBlock* f64_body = llvm::BasicBlock::Create(globalContext(), "pv.f64.body", fn);
  builder_.CreateCondBr(is_tag(3), f64_body, bool_bb);
  builder_.SetInsertPoint(f64_body);
  llvm::Value* f64_text = nullptr;
  {
    llvm::Value* num = builder_.CreateCall(load_f64, {cell()}, "pv.f64.val");
    f64_text = builder_.CreateCall(f64_str, {num}, "pv.f64.text");
    builder_.CreateBr(merge_bb);
  }
  llvm::BasicBlock* f64_end = builder_.GetInsertBlock();

  // tag 4 → bool cell (i32 0/1)
  builder_.SetInsertPoint(bool_bb);
  llvm::BasicBlock* bool_body = llvm::BasicBlock::Create(globalContext(), "pv.bool.body", fn);
  builder_.CreateCondBr(is_tag(4), bool_body, bad_bb);
  builder_.SetInsertPoint(bool_body);
  llvm::Value* bool_text = nullptr;
  {
    llvm::Value* num = builder_.CreateCall(load_i32, {cell()}, "pv.bool.val");
    llvm::Value* non_zero =
        builder_.CreateICmpNE(num, llvm::ConstantInt::get(i32Ty(), 0), "pv.bool.nz");
    llvm::Value* t = builder_.CreateGlobalStringPtr("true", "pv.true");
    llvm::Value* f = builder_.CreateGlobalStringPtr("false", "pv.false");
    bool_text = builder_.CreateSelect(toI1(non_zero), t, f, "pv.bool.text");
    builder_.CreateBr(merge_bb);
  }
  llvm::BasicBlock* bool_end = builder_.GetInsertBlock();

  // Unknown tag → a marker, same as the previous Optional-print behaviour.
  builder_.SetInsertPoint(bad_bb);
  llvm::Value* bad_text = nullptr;
  {
    bad_text = builder_.CreateGlobalStringPtr("[Variant]", "pv.unknown");
    builder_.CreateBr(merge_bb);
  }
  llvm::BasicBlock* bad_end = builder_.GetInsertBlock();

  // Single print site: all five arms merge into one C string.
  builder_.SetInsertPoint(merge_bb);
  llvm::PHINode* text = builder_.CreatePHI(ptrTy(), 5, "pv.text");
  text->addIncoming(i32_text, i32_end);
  text->addIncoming(str_text, str_end);
  text->addIncoming(f64_text, f64_end);
  text->addIncoming(bool_text, bool_end);
  text->addIncoming(bad_text, bad_end);
  builder_.CreateCall(print_str, {text});

  llvm::Value* nl = builder_.CreateGlobalString("\n", "nl");
  builder_.CreateCall(print_str, {nl});
  return llvm::ConstantInt::get(i32Ty(), 0);
}

// `print(opt)` where opt:Optional<Variant> — payload when present, marker when empty.
llvm::Value* Codegen::emitPrintOptionalVariant(const ast::Expr& arg_expr, llvm::Value* opt_val,
                                               const ast::SourceLocation& loc) {
  const std::string opt_ty = "Optional<Variant>";
  if (!ensureTypeAvailable(opt_ty)) {
    return nullptr;
  }
  const ClassInfo* oinfo = findClass(opt_ty);
  if (!oinfo || !oinfo->type) {
    error(loc, "print(Optional<Variant>) requires the Optional<Variant> type");
    return nullptr;
  }
  if (opt_val->getType()->isPointerTy()) {
    opt_val = builder_.CreateLoad(oinfo->type, opt_val, "pov.opt");
  }
  llvm::Value* has = builder_.CreateExtractValue(opt_val, 0, "pov.has");
  llvm::Value* payload = builder_.CreateExtractValue(opt_val, 1, "pov.payload");

  llvm::Function* print_str = requireExtern("l_print_str", loc);
  if (!print_str) {
    return nullptr;
  }
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* some_bb = llvm::BasicBlock::Create(globalContext(), "pov.some", fn);
  llvm::BasicBlock* none_bb = llvm::BasicBlock::Create(globalContext(), "pov.none", fn);
  llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(globalContext(), "pov.exit", fn);
  builder_.CreateCondBr(toI1(has), some_bb, none_bb);

  // Present: the shared Variant printer (payload branch + newline).
  builder_.SetInsertPoint(some_bb);
  llvm::Value* some_ret = emitPrintVariant(arg_expr, payload, loc);
  if (!some_ret) {
    return nullptr;
  }
  builder_.CreateBr(exit_bb);

  // Absent: an empty line, matching the previous `print("")` behaviour.
  builder_.SetInsertPoint(none_bb);
  llvm::Value* empty = builder_.CreateGlobalStringPtr("", "pov.empty");
  builder_.CreateCall(print_str, {empty});
  llvm::Value* nl = builder_.CreateGlobalString("\n", "pov.nl");
  builder_.CreateCall(print_str, {nl});
  builder_.CreateBr(exit_bb);

  builder_.SetInsertPoint(exit_bb);
  return llvm::ConstantInt::get(i32Ty(), 0);
}

llvm::Type* Codegen::elementTypeForPtrIndex() const {
  if (current_class_.empty()) {
    return nullptr;
  }
  const ClassInfo* info = findClass(current_class_);
  if (!info || info->type_args.empty()) {
    return nullptr;
  }
  return const_cast<Codegen*>(this)->llvmTypeForTypeName(info->type_args[0]);
}

llvm::Type* Codegen::elementTypeForIndexedBase(const ast::Expr& object) const {
  std::string name;
  if (object.kind() == ast::ExprKind::Identifier) {
    name = static_cast<const ast::IdentifierExpr&>(object).name;
  }
  // Map / grow temporaries: parallel typed buffers behind raw ptr fields/locals.
  // Source uses logical names (keys); physical LLVM fields remain keys_.
  if (name == "keys" || name == "keys_" || name == "old_keys") {
    return const_cast<Codegen*>(this)->ptrTy();
  }
  if (name == "occupied" || name == "occupied_" || name == "old_occupied") {
    return const_cast<Codegen*>(this)->i1Ty();
  }
  if (name == "values" || name == "values_" || name == "data" || name == "data_" ||
      name == "old_values") {
    return elementTypeForPtrIndex();
  }
  return elementTypeForPtrIndex();
}

std::string Codegen::instantiateMemberType(const std::string& type_name,
                                           const ClassInfo& info) const {
  if (!info.ast || info.type_args.empty()) {
    return resolveTypeName(type_name);
  }
  // fun(T) / fun(T)->R: recurse into param/return slots so T is substituted.
  if (isFunTypeName(type_name)) {
    std::vector<std::string> params;
    std::string ret;
    bool has_ret = false;
    if (!parseFunTypeName(type_name, params, ret, has_ret)) {
      return resolveTypeName(type_name);
    }
    std::string out = "fun(";
    for (size_t i = 0; i < params.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      out += instantiateMemberType(params[i], info);
    }
    out.push_back(')');
    if (has_ret) {
      out += "->";
      out += instantiateMemberType(ret, info);
    }
    return out;
  }
  std::string out = type_name;
  for (size_t i = 0; i < info.ast->generics.size() && i < info.type_args.size(); ++i) {
    const std::string& g = info.ast->generics[i].name;
    const std::string& concrete = info.type_args[i];
    if (out == g) {
      out = concrete;
      continue;
    }
    const std::string pattern = "<" + g + ">";
    const std::string repl = "<" + concrete + ">";
    for (;;) {
      const auto pos = out.find(pattern);
      if (pos == std::string::npos) {
        break;
      }
      out.replace(pos, pattern.size(), repl);
    }
  }
  return resolveTypeName(out);
}

llvm::Value* Codegen::emitIndexLValue(const ast::IndexExpr& expr) {
  if (!expr.object || !expr.index) {
    error(expr.loc, "incomplete index expression");
    return nullptr;
  }

  // Array<T> / specialized class: arr[i] → GEP on data_ with elem = T
  if (expr.object->kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(*expr.object);
    auto lit = local_types_.find(id.name);
    if (lit != local_types_.end() && isClassType(lit->second)) {
      const ClassInfo* info = findClass(lit->second);
      if (!info || info->type_args.empty()) {
        error(expr.loc, "index on non-generic class " + lit->second);
        return nullptr;
      }
      auto fit = info->field_index.find("data_");
      if (fit == info->field_index.end()) {
        error(expr.loc, "indexed class missing data_ field");
        return nullptr;
      }
      llvm::Value* obj = classObjectPtr(id.name, expr.loc);
      if (!obj) {
        return nullptr;
      }
      llvm::Value* data_gep =
          builder_.CreateStructGEP(info->type, obj, fit->second, "arr.data");
      llvm::Value* data = builder_.CreateLoad(ptrTy(), data_gep, "arr.data.ptr");
      llvm::Value* idx = toI32(emitExpr(*expr.index), expr.loc);
      if (!idx) {
        return nullptr;
      }
      llvm::Type* elem_ty = llvmTypeForTypeName(info->type_args[0]);
      return builder_.CreateInBoundsGEP(elem_ty, data, idx, "arr.idx");
    }
  }

  // Inside Array/Map methods: field_[i] / old_keys[i] on a raw ptr buffer.
  llvm::Value* base = emitExpr(*expr.object);
  if (!base) {
    return nullptr;
  }
  if (!base->getType()->isPointerTy()) {
    error(expr.loc, "index base must be a pointer or Array");
    return nullptr;
  }
  llvm::Type* elem_ty = elementTypeForIndexedBase(*expr.object);
  if (!elem_ty) {
    error(expr.loc, "cannot determine element type for pointer index (need generic class context)");
    return nullptr;
  }
  llvm::Value* idx = toI32(emitExpr(*expr.index), expr.loc);
  if (!idx) {
    return nullptr;
  }
  return builder_.CreateInBoundsGEP(elem_ty, base, idx, "ptr.idx");
}

llvm::Value* Codegen::emitSizeof(const ast::SizeofExpr& expr) {
  const uint64_t bytes = allocSizeOfTypeName(expr.type_name);
  return llvm::ConstantInt::get(i32Ty(), static_cast<uint64_t>(bytes), false);
}

llvm::Value* Codegen::emitAddrOf(const ast::AddrOfExpr& expr) {
  if (!expr.operand) {
    error(expr.loc, "address-of missing operand");
    return nullptr;
  }
  // Allow taking the address of globals (including mutable `!`) and locals.
  if (expr.operand->kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(*expr.operand);
    if (const_values_.count(id.name) != 0) {
      error(expr.loc, "cannot take address of compile-time const '" + id.name + "'");
      return nullptr;
    }
    if (llvm::Value* storage = lookupStorage(id.name)) {
      return builder_.CreateBitCast(storage, ptrTy(), id.name + ".addr");
    }
    error(expr.loc, "unknown variable in address-of: " + id.name);
    return nullptr;
  }
  llvm::Value* slot = emitLValue(*expr.operand);
  if (!slot) {
    return nullptr;
  }
  return builder_.CreateBitCast(slot, ptrTy(), "addr");
}

// Address of a field of the current class, reached through `this`.
// Returns nullptr (with a diagnostic) for static methods.
llvm::Value* Codegen::fieldAddressOnThis(const std::string& field,
                                         const ast::SourceLocation& loc) {
  const ClassInfo* info = findClass(current_class_);
  if (!info) {
    return nullptr;
  }
  auto fit = info->field_index.find(physicalFieldName(field));
  if (fit == info->field_index.end()) {
    return nullptr;
  }
  if (current_method_is_static_ || !current_this_) {
    error(loc, "cannot access instance field '" + field + "' from a static method");
    return nullptr;
  }
  return builder_.CreateStructGEP(info->type, current_this_, fit->second, field);
}

llvm::Value* Codegen::emitLValue(const ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);

      if (const_values_.count(id.name) != 0) {
        error(expr.loc, "cannot assign to const '" + id.name + "'");
        return nullptr;
      }

      auto cap = capture_slots_.find(id.name);
      if (cap != capture_slots_.end() && current_lambda_ctx_ && current_lambda_ctx_ty_) {
        llvm::Value* slot = builder_.CreateStructGEP(current_lambda_ctx_ty_, current_lambda_ctx_,
                                                     cap->second, id.name + ".cap");
        if (current_lambda_ctx_by_value_) {
          return slot;  // store directly into heap-promoted capture
        }
        return builder_.CreateLoad(ptrTy(), slot, id.name + ".ptr");
      }

      if (id.name == "this" && current_method_is_static_) {
        error(expr.loc, "cannot use 'this' inside a static method");
        return nullptr;
      }
      // Method body: bare field name resolves against this (logical → physical).
      if (!current_class_.empty()) {
        if (llvm::Value* field = fieldAddressOnThis(id.name, id.loc)) {
          return field;
        }
      }

      if (llvm::Value* storage = lookupStorage(id.name)) {
        if (global_readonly_.count(id.name) != 0) {
          error(expr.loc, "cannot assign to constd '" + id.name + "'");
          return nullptr;
        }
        return storage;
      }
      error(expr.loc, "unknown variable: " + id.name);
      return nullptr;
    }
    case ast::ExprKind::MemberAccess: {
      const auto& access = static_cast<const ast::MemberAccessExpr&>(expr);
      if (!access.object) {
        error(expr.loc, "member access missing object");
        return nullptr;
      }
      std::string obj_ty = exprTypeOf(*access.object);
      if (obj_ty.empty()) {
        obj_ty = inferTypeFromExpr(*access.object);
      }
      if (!isClassType(obj_ty)) {
        error(expr.loc, "member access on non-class value");
        return nullptr;
      }
      ensureTypeAvailable(obj_ty);
      const ClassInfo* info = findClass(obj_ty);
      if (!info) {
        error(expr.loc, "bad class object in member access");
        return nullptr;
      }
      const PropResolve pr = resolveProperty(*info, access.member, PropAccess::Write);
      if (pr.kind == PropResolveKind::Method) {
        error(expr.loc, "property '" + access.member + "' uses setter; not an lvalue");
        return nullptr;
      }
      if (pr.kind != PropResolveKind::Field) {
        error(expr.loc, "unknown property '" + access.member +
                              "' (no set_" + access.member + " / pub " + access.member +
                              "_)");
        return nullptr;
      }
      llvm::Value* base = classValuePtr(*access.object, obj_ty);
      if (!base) {
        return nullptr;
      }
      auto fit = info->field_index.find(pr.field_name);
      return builder_.CreateStructGEP(info->type, base, fit->second, pr.field_name);
    }
    case ast::ExprKind::Index:
      return emitIndexLValue(static_cast<const ast::IndexExpr&>(expr));
    default:
      error(expr.loc, "expression is not an assignable lvalue");
      return nullptr;
  }
}

// Declared type name of the first parameter of `name` in `cls`, if any.
std::string Codegen::firstParamTypeName(const ast::ClassDeclStmt& cls,
                                        const std::string& name) const {
  for (const auto& m : cls.methods) {
    if (!m || m->name != name || m->params.empty()) {
      continue;
    }
    return resolveTypeName(m->params[0].type_name);
  }
  return {};
}

bool Codegen::emitAssign(const ast::AssignStmt& assign) {
  if (!assign.target || !assign.value) {
    error(assign.loc, "incomplete assignment");
    return false;
  }

  // [dsl] Bare `port = 8080` inside a block: on a `CPropertys` root it
  // lowers to `this.set("port", 8080)`; a typed root resolves the field instead.
  if (assign.is_dsl_property && current_this_ && !current_class_.empty()) {
    const auto& access = static_cast<const ast::MemberAccessExpr&>(*assign.target);
    const ClassInfo* info = findClass(current_class_);
    if (!info) {
      error(assign.loc, "unknown config class '" + current_class_ + "'");
      return false;
    }
    // `CPropertys` roots have no typed fields: store name → value.
    if (current_class_ == "CPropertys") {
      auto sit = info->methods.find("set");
      if (sit == info->methods.end()) {
        error(assign.loc, "CPropertys is missing set()");
        return false;
      }
      llvm::Value* key = makeStringLitObject(access.member, "prop.key", assign.loc);
      llvm::Value* val = emitValueForParam(
          *assign.value, sit->second->getFunctionType()->getParamType(2), "Variant");
      if (!val) {
        return false;
      }
      builder_.CreateCall(sit->second, {current_this_, key, val});
      return true;
    }
    const PropResolve pr = resolveProperty(*info, access.member, PropAccess::Write);
    if (pr.kind == PropResolveKind::None) {
      error(access.loc,
            "unknown property '" + access.member + "' on '" + current_class_ + "'");
      return false;
    }
    // Setter: `this.set_x(value)`.
    if (pr.kind == PropResolveKind::Method) {
      auto mit = info->methods.find(pr.method_name);
      if (mit == info->methods.end()) {
        error(assign.loc, "missing setter " + pr.method_name);
        return false;
      }
      std::string expected_name;
      if (info->ast) {
        for (const auto& m : info->ast->methods) {
          if (m && m->name == pr.method_name && !m->params.empty()) {
            expected_name = resolveTypeName(m->params[0].type_name);
            break;
          }
        }
      }
      llvm::Type* expected = mit->second->getFunctionType()->getParamType(1);
      llvm::Value* val = emitValueForParam(*assign.value, expected, expected_name);
      if (!val) {
        return false;
      }
      builder_.CreateCall(mit->second, {current_this_, val});
      return true;
    }
    // Field: `this.<physical> = value`.
    auto fit = info->field_index.find(pr.field_name);
    if (fit == info->field_index.end()) {
      error(assign.loc, "missing field " + pr.field_name);
      return false;
    }
    llvm::Value* slot =
        builder_.CreateStructGEP(info->type, current_this_, fit->second, "prop.slot");
    std::string field_ty;
    if (fit->second < info->field_types.size()) {
      field_ty = resolveTypeName(info->field_types[fit->second]);
    }
    llvm::Type* fty = info->type->getElementType(fit->second);
    llvm::Value* val = emitValueForParam(*assign.value, fty, field_ty);
    if (!val) {
      return false;
    }
    // Class-typed fields are stored by value: a literal / method result arrives
    // as a `String*` (or Class*) and must be copied into the field slot.
    if ((isClassType(field_ty) || field_ty == "String") && val->getType()->isPointerTy() &&
        fty->isStructTy()) {
      val = builder_.CreateLoad(fty, val, "prop.copy");
    }
    builder_.CreateStore(val, slot);
    return true;
  }

  if (rejectReadonlyMutation(*assign.target, assign.loc)) {
    return false;
  }

  // Interface reassignment from class: iface = p;
  if (assign.target->kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(*assign.target);
    auto lit = local_types_.find(id.name);
    if (lit != local_types_.end() && isInterfaceType(lit->second)) {
      auto vit = named_values_.find(id.name);
      if (vit == named_values_.end()) {
        error(assign.loc, "unknown interface variable: " + id.name);
        return false;
      }
      if (assign.value->kind() != ast::ExprKind::Identifier) {
        error(assign.loc, "interface assign expects class instance");
        return false;
      }
      const auto& src = static_cast<const ast::IdentifierExpr&>(*assign.value);
      auto slit = local_types_.find(src.name);
      if (slit == local_types_.end() || !isClassType(slit->second)) {
        error(assign.loc, "interface assign source must be a class local");
        return false;
      }
      llvm::Value* obj = classObjectPtr(src.name, assign.loc);
      if (!obj) {
        return false;
      }
      llvm::Value* fat = boxToInterface(obj, slit->second, lit->second, assign.loc);
      if (!fat) {
        return false;
      }
      builder_.CreateStore(fat, vit->second);
      return true;
    }
  }

  // Property write: `u.score = 10` → set_score(u, 10) or store pub score_.
  if (assign.target->kind() == ast::ExprKind::MemberAccess) {
    const auto& access = static_cast<const ast::MemberAccessExpr&>(*assign.target);
    if (access.object) {
      std::string obj_ty = exprTypeOf(*access.object);
      if (obj_ty.empty()) {
        obj_ty = inferTypeFromExpr(*access.object);
      }
      if (isClassType(obj_ty)) {
        ensureTypeAvailable(obj_ty);
        const ClassInfo* info = findClass(obj_ty);
        if (!info) {
          error(assign.loc, "bad class in property assign");
          return false;
        }
        const PropResolve pr = resolveProperty(*info, access.member, PropAccess::Write);
        if (pr.kind == PropResolveKind::Method) {
          auto mit = info->methods.find(pr.method_name);
          if (mit == info->methods.end()) {
            error(assign.loc, "missing setter " + pr.method_name);
            return false;
          }
          llvm::Value* self = classValuePtr(*access.object, obj_ty);
          if (!self) {
            return false;
          }
          std::string expected_name;
          if (info->ast) {
            expected_name = firstParamTypeName(*info->ast, pr.method_name);
          }
          llvm::Type* expected = mit->second->getFunctionType()->getParamType(1);
          llvm::Value* arg = emitValueForParam(*assign.value, expected, expected_name);
          if (!arg) {
            return false;
          }
          builder_.CreateCall(mit->second, {self, arg});
          return true;
        }
        if (pr.kind == PropResolveKind::Field) {
          llvm::Value* self = classValuePtr(*access.object, obj_ty);
          if (!self) {
            return false;
          }
          auto fit = info->field_index.find(pr.field_name);
          llvm::Value* gep =
              builder_.CreateStructGEP(info->type, self, fit->second, pr.field_name);
          const std::string& fty_name = info->field_types[fit->second];
          llvm::Value* v = emitValueForParam(*assign.value, llvmTypeForTypeName(fty_name),
                                             fty_name);
          if (!v) {
            return false;
          }
          if (isClassType(fty_name) && v->getType()->isPointerTy()) {
            llvm::Value* loaded =
                builder_.CreateLoad(findClass(fty_name)->type, v, "prop.copy");
            builder_.CreateStore(loaded, gep);
          } else {
            builder_.CreateStore(v, gep);
          }
          return true;
        }
        error(assign.loc, "unknown property '" + access.member + "' (no set_" +
                              access.member + " / pub " + access.member + "_)");
        return false;
      }
    }
  }

  llvm::Value* ptr = emitLValue(*assign.target);
  if (!ptr) {
    return false;
  }
  // String slot assigned a literal: materialise a String object instead of
  // handing emitExpr a bare cstr (the generic struct path below would load
  // `%string_String` straight out of the literal's bytes).
  llvm::Type* slot_ty = nullptr;
  if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(ptr)) {
    slot_ty = ai->getAllocatedType();
  } else if (auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(ptr)) {
    slot_ty = gv->getValueType();
  }
  llvm::Value* v = nullptr;
  if (slot_ty && slot_ty->isStructTy() && slot_ty->getStructName() == "string_String" &&
      assign.value->kind() == ast::ExprKind::StringLiteral) {
    llvm::Value* tmp = emitStringObjectPtr(*assign.value);
    if (!tmp) {
      return false;
    }
    v = builder_.CreateLoad(slot_ty, tmp, "store.string.copy");
  } else {
    v = emitExpr(*assign.value);
  }
  if (!v) {
    return false;
  }
  if (assign.target->kind() == ast::ExprKind::Index) {
    const auto& ix = static_cast<const ast::IndexExpr&>(*assign.target);
    llvm::Type* elem_ty = nullptr;
    if (ix.object) {
      elem_ty = elementTypeForIndexedBase(*ix.object);
    }
    if (!elem_ty) {
      elem_ty = elementTypeForPtrIndex();
    }
    if (elem_ty && elem_ty->isIntegerTy(1)) {
      v = toI1(v);
    } else if (elem_ty && elem_ty->isIntegerTy(32) && !v->getType()->isPointerTy()) {
      v = toI32(v, assign.loc);
    } else if (elem_ty && elem_ty->isStructTy() && v->getType()->isPointerTy()) {
      // Class/struct elements (e.g. Array<String>): ABI value is Class*; store by value.
      v = builder_.CreateLoad(elem_ty, v, "idx.store.copy");
    } else if (elem_ty && elem_ty->isPointerTy() && !v->getType()->isPointerTy()) {
      error(assign.loc, "pointer index store expects a pointer value");
      return false;
    }
  } else {
    llvm::Type* dest_ty = nullptr;
    if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(ptr)) {
      dest_ty = ai->getAllocatedType();
    } else if (auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(ptr)) {
      dest_ty = gv->getValueType();
    }
    if (!dest_ty && assign.target->kind() == ast::ExprKind::Identifier) {
      // Field slot: `props = Map<Variant>()` where the field is an inline struct.
      // Only inline struct slots need value stores; pointer slots keep the ptr.
      if (!current_class_.empty()) {
        const auto& tid = static_cast<const ast::IdentifierExpr&>(*assign.target);
        if (const ClassInfo* owner = findClass(current_class_)) {
          auto fit = owner->field_index.find(physicalFieldName(tid.name));
          if (fit != owner->field_index.end()) {
            dest_ty = owner->type->getElementType(fit->second);
          }
        }
      }
    }
    if (dest_ty && v->getType() == dest_ty) {
      // Exact match (e.g. an inline `Variant` field assigned from a `%Variant`
      // value) — store as-is; no integer coercion applies.
    } else if (dest_ty && dest_ty->isStructTy() && v->getType() == dest_ty->getPointerTo()) {
      // Inline struct field/element: ABI passes classes by pointer; store by value.
      // Only deref when the value really points at that struct (not a raw cstr).
      v = builder_.CreateLoad(dest_ty, v, "store.struct.copy");
    } else if (dest_ty && dest_ty->isPointerTy() && v->getType()->isStructTy()) {
      // [link:109-Performance-First-Architecture] A `:=` String/class local is boxed to `ptr` (8 bytes),
      // but a method like `concat` hands back the class by value. Storing the
      // whole struct into the pointer slot overflows the alloca and corrupts
      // neighbouring locals. Box the value on the heap and keep the pointer.
      llvm::Value* boxed = builder_.CreateCall(
          requireExtern("l_alloc", assign.loc),
          {llvm::ConstantInt::get(i64Ty(), module_->getDataLayout().getTypeAllocSize(
                                               v->getType()))},
          "assign.box");
      builder_.CreateStore(v, boxed);
      v = boxed;
    } else if (dest_ty && dest_ty->isStructTy() && v->getType()->isPointerTy()) {
      // Raw pointer into a struct slot: bitcast then load.
      llvm::Value* casted = builder_.CreateBitCast(v, dest_ty->getPointerTo(), "store.cast");
      v = builder_.CreateLoad(dest_ty, casted, "store.struct.copy");
    } else if (dest_ty && dest_ty->isIntegerTy(64) && v->getType()->isIntegerTy()) {
      v = builder_.CreateIntCast(v, i64Ty(), /*isSigned=*/true, "store.i64");
    } else if (dest_ty && dest_ty->isDoubleTy()) {
      if (v->getType()->isIntegerTy()) {
        v = builder_.CreateSIToFP(v, f64Ty(), "store.f64");
      }
    } else if (dest_ty && dest_ty->isIntegerTy(1)) {
      v = toI1(v);
    } else if (dest_ty && dest_ty->isIntegerTy(32) && v->getType()->isIntegerTy()) {
      v = toI32(v, assign.loc);
    }
  }
  builder_.CreateStore(v, ptr);
  return true;
}

// [link:50-Variant] `return v` where `v : Variant` and the enclosing function
// returns String/i32/i64/f64/bool. Lowers to `v.as_<t>()` plus a runtime tag
// check that panics on mismatch — the return type is a promise the tag must keep.
// `immediate` keeps the success path in the caller's block; otherwise the
// expression is materialized in its own block (see the panic branch below).
llvm::Value* Codegen::emitContextualVariantUnbox(const ast::Expr& expr,
                                                 const std::string& expected_type,
                                                 llvm::Type* rty, bool immediate) {
  (void)immediate;
  const std::string want = expected_type;
  if (want.empty() || want == "Variant") {
    return nullptr;
  }
  std::string kind;
  if (want == "String") {
    kind = "String";
  } else if (want == "i32" || want == "i64") {
    kind = "i32";
  } else if (want == "f64") {
    kind = "f64";
  } else if (want == "bool") {
    kind = "bool";
  } else {
    return nullptr;
  }
  // Only `Variant` sources unbox implicitly.
  if (resolveTypeName(exprTypeOf(expr)) != "Variant") {
    return nullptr;
  }
  const std::string opt_ty = "Optional<" + kind + ">";
  if (!ensureTypeAvailable("Variant") || !ensureTypeAvailable(opt_ty)) {
    return nullptr;
  }
  llvm::Value* val = emitExpr(expr);
  if (!val) {
    return nullptr;
  }
  const ClassInfo* vinfo = findClass("Variant");
  const ClassInfo* oinfo = findClass(opt_ty);
  if (!vinfo || !vinfo->type || !oinfo || !oinfo->type) {
    return nullptr;
  }
  // Class values travel as pointers in this backend; normalize to a value.
  if (val->getType()->isPointerTy()) {
    val = builder_.CreateLoad(vinfo->type, val, "unbox.variant");
  }
  // Materialize the Variant so `as_<t>()` can take its address.
  const std::string tmp_name = "__unbox_" + std::to_string(coalesce_tmp_counter_++);
  llvm::Function* cur_fn = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* slot = createEntryAlloca(cur_fn, vinfo->type, tmp_name);
  builder_.CreateStore(val, slot);
  local_types_[tmp_name] = "Variant";
  named_values_[tmp_name] = slot;

  ast::MethodCallExpr as_call;
  as_call.loc = expr.loc;
  auto id = std::make_unique<ast::IdentifierExpr>();
  id->loc = expr.loc;
  id->name = tmp_name;
  as_call.object = std::move(id);
  as_call.method = "as_" + std::string(kind == "String" ? "string" : kind.c_str());
  llvm::Value* opt = emitMethodCall(as_call);
  named_values_.erase(tmp_name);
  local_types_.erase(tmp_name);
  if (!opt) {
    return nullptr;
  }
  if (opt->getType()->isPointerTy()) {
    opt = builder_.CreateLoad(oinfo->type, opt, "unbox.opt");
  }
  if (opt->getType() != oinfo->type) {
    error(expr.loc, "contextual Variant unbox produced an unexpected " + opt_ty);
    return nullptr;
  }
  // has_value_ is field 0; value_ follows it.
  llvm::Value* has = builder_.CreateExtractValue(opt, 0, "unbox.has");
  llvm::Value* payload = builder_.CreateExtractValue(opt, 1, "unbox.val");

  auto emit_panic = [&](llvm::BasicBlock* label) -> bool {
    builder_.SetInsertPoint(label);
    llvm::Function* panic_fn = requireExtern("l_panic", expr.loc);
    if (!panic_fn) {
      return false;
    }
    llvm::Function* str_fn = requireExtern("l_str_concat", expr.loc);
    llvm::Value* msg =
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy()));
    if (str_fn) {
      llvm::Value* head = builder_.CreateGlobalStringPtr(
          "[PANIC] Variant does not hold a ", "unbox.panic.head");
      llvm::Value* tail = builder_.CreateGlobalStringPtr(kind.c_str(), "unbox.panic.tail");
      msg = builder_.CreateCall(str_fn, {head, tail}, "unbox.panic.msg");
    }
    builder_.CreateCall(panic_fn, {msg});
    builder_.CreateUnreachable();
    return true;
  };

  // The caller may still be mid-expression (`print(v ?? "x")`, `n := <expr>`), so
  // the success path must continue in a block that dominates the caller's code.
  // Both paths fall through to a fresh block: the success arm records `ok`, the
  // panic arm is terminated, and emission resumes on the fallthrough block.
  llvm::BasicBlock* ok_bb = llvm::BasicBlock::Create(globalContext(), "unbox.ok", cur_fn);
  llvm::BasicBlock* bad_bb = llvm::BasicBlock::Create(globalContext(), "unbox.bad", cur_fn);
  builder_.CreateCondBr(toI1(has), ok_bb, bad_bb);
  if (!emit_panic(bad_bb)) {
    return nullptr;
  }
  builder_.SetInsertPoint(ok_bb);
  // Primitives may need widening; String stays a pointer to the String object.
  if (rty && rty->isIntegerTy(32) && !payload->getType()->isPointerTy()) {
    payload = toI32(payload, expr.loc);
  }
  return payload;
}

bool Codegen::emitReturn(const ast::ReturnStmt& ret) {
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  if (in_coroutine_) {
    // `async fun` returns `ptr` (the task) at the LLVM level; the business
    // return type lives in current_fn_return_type_ and is packed into the task.
    llvm::Value* res = nullptr;
    if (ret.value) {
      cancelAutoFreeEscaping(*ret.value);
      llvm::Type* biz_ty = typeFromName(current_fn_return_type_);
      llvm::Value* rv = emitExpr(*ret.value);
      if (!rv) {
        return false;
      }
      if (biz_ty->isPointerTy() && rv->getType()->isPointerTy()) {
        res = rv;
      } else if (biz_ty->isStructTy() && rv->getType()->isPointerTy()) {
        res = builder_.CreateLoad(biz_ty, rv, "ret.struct");
      } else if (biz_ty->isIntegerTy(1)) {
        res = toI1(rv);
      } else if (biz_ty->isIntegerTy(32) && !rv->getType()->isPointerTy()) {
        res = toI32(rv, ret.loc);
      } else {
        res = rv;
      }
    }
    if (!finishCleanups()) {
      return false;
    }
    publishCoroResult(res);
    return true;
  }
  llvm::Value* v = nullptr;
  if (ret.value) {
    // [link:75-Escape-Analysis-Auto-Heap-Promotion]: returning an escaping capturing lambda → fat closure {fn, ctx}.
    if (ret.value->kind() == ast::ExprKind::Lambda) {
      const auto& lam = static_cast<const ast::LambdaExpr&>(*ret.value);
      if (lam.escapes) {
        std::string lam_ty = inferTypeFromExpr(*ret.value);
        if (lam_ty.empty()) {
          lam_ty = "fun()->i32";
        }
        pending_closure_ctx_ = nullptr;
        pending_closure_ctx_ty_ = nullptr;
        llvm::Value* fn_ptr = emitLambda(lam, lam_ty);
        if (!fn_ptr || !pending_closure_ctx_) {
          error(ret.loc, "escaping lambda failed to produce fn/ctx");
          return false;
        }
        llvm::Value* fat = llvm::UndefValue::get(fatClosureTy());
        fat = builder_.CreateInsertValue(fat, fn_ptr, 0, "esc.fn");
        fat = builder_.CreateInsertValue(fat, pending_closure_ctx_, 1, "esc.ctx");
        pending_closure_ctx_ = nullptr;
        pending_closure_ctx_ty_ = nullptr;
        v = fat;
        if (!finishCleanups()) {
          return false;
        }
        emitLifecycleDeinitCall();
        builder_.CreateRet(v);
        return true;
      }
    }
    // Escaping a heap local via return (often `as ptr`) must not auto-free it.
    cancelAutoFreeEscaping(*ret.value);
    // [link:50-Variant] `return v` with v:Variant into a primitive/String signature.
    llvm::Value* unboxed =
        emitContextualVariantUnbox(*ret.value, current_fn_return_type_, fn->getReturnType());
    v = unboxed ? unboxed : emitExpr(*ret.value);
    if (!v) {
      return false;
    }
    llvm::Type* rty = fn->getReturnType();
    if (rty->isStructTy()) {
      // NewExpr yields an alloca pointer; class returns are by value.
      if (v->getType()->isPointerTy()) {
        v = builder_.CreateLoad(rty, v, "ret.struct");
      }
    } else if (rty->isIntegerTy(1)) {
      v = toI1(v);
    } else if (rty->isIntegerTy(32) && !v->getType()->isPointerTy()) {
      v = toI32(v, ret.loc);
    } else if (rty->isPointerTy() && v->getType()->isPointerTy()) {
      // already ptr
    } else if (rty != v->getType() && rty->isIntegerTy() && v->getType()->isIntegerTy()) {
      v = builder_.CreateIntCast(v, rty, /*isSigned=*/true, "retcast");
    }
  }
  // Evaluate return value first, then defers + auto-free, then lifecycle deinit, then ret.
  if (!finishCleanups()) {
    return false;
  }
  emitLifecycleDeinitCall();
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

bool Codegen::emitPatternBindings(const ast::MatchPattern& pat, llvm::Value* slot,
                                  std::vector<std::pair<std::string, llvm::Value*>>& bound) {
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  if (pat.kind == ast::MatchPatternKind::ClassDestructure) {
    const std::string ty = resolveTypeName(pat.class_name);
    if (!ensureTypeAvailable(ty)) {
      return false;
    }
    const ClassInfo* info = findClass(ty);
    if (!info) {
      error(pat.loc, "unknown class type in destructuring pattern: '" + pat.class_name + "'");
      return false;
    }
    if (!slot->getType()->isPointerTy()) {
      error(pat.loc, "destructuring pattern requires a class value");
      return false;
    }
    for (const ast::PatternField& pf : pat.fields) {
      if (pf.name.empty() || pf.name == "_") {
        continue;
      }
      // Field access mirrors property sugar: a getter is not a field, and
      // destructuring must stay a raw GEP + load ([link:109-Performance-First-Architecture]).
      const PropResolve res = resolveProperty(*info, pf.name, PropAccess::Read);
      if (res.kind == PropResolveKind::Method) {
        error(pf.loc, "cannot destructure '" + ty + "." + pf.name +
                          "': it is exposed by a getter, not a field");
        return false;
      }
      if (res.kind != PropResolveKind::Field) {
        error(pf.loc, "unknown field '" + pf.name + "' in destructuring pattern for '" + ty + "'");
        return false;
      }
      const unsigned idx = info->field_index.at(res.field_name);
      if (idx >= info->field_types.size()) {
        error(pf.loc, "internal: field '" + pf.name + "' has no recorded type");
        return false;
      }
      const std::string field_ty = info->field_types[idx];
      llvm::Value* field_slot = builder_.CreateStructGEP(info->type, slot, idx,
                                                         "pat.field." + pf.name);
      if (pf.sub) {
        // Nested pattern: class fields are inline, so the child pattern walks
        // the sub-struct in place (GEPs off the same base object).
        if (!emitPatternBindings(*pf.sub, field_slot, bound)) {
          return false;
        }
        continue;
      }
      const std::string bind = pf.binding.empty() ? pf.name : pf.binding;
      if (bind == "_") {
        continue;
      }
      const bool class_field = isClassType(field_ty) && findClass(field_ty) != nullptr;
      llvm::AllocaInst* dst = nullptr;
      if (class_field) {
        // Class fields are stored inline; locals then hold a pointer to the copy.
        dst = createEntryAlloca(fn, findClass(field_ty)->type, bind + ".inline");
        llvm::Value* copy = builder_.CreateLoad(findClass(field_ty)->type, field_slot,
                                                "pat.copy." + bind);
        builder_.CreateStore(copy, dst);
      } else {
        llvm::Type* val_ty = llvmTypeForTypeName(field_ty);
        if (!val_ty) {
          val_ty = i32Ty();
        }
        dst = createEntryAlloca(fn, val_ty, bind);
        builder_.CreateStore(builder_.CreateLoad(val_ty, field_slot, "pat.val." + bind), dst);
      }
      bound.emplace_back(bind, dst);
      named_values_[bind] = dst;
      local_types_[bind] = field_ty;
    }
    return true;
  }

  // `[a, b]` — binds the first N elements of an Array<T>. The match arm only
  // runs when length >= N, so indices stay in bounds; the element type comes
  // from the scrutinee (`Array<T>`), loaded off `data_[i]`.
  if (pat.kind == ast::MatchPatternKind::ArrayDestructure) {
    std::string base;
    std::vector<std::string> args;
    if (!splitGenericType(scrut_type_name_, base, args) || base != "Array" || args.size() != 1) {
      error(pat.loc, "array pattern requires an Array scrutinee, got '" + scrut_type_name_ + "'");
      return false;
    }
    const std::string elem_ty = args[0];
    llvm::Type* elem_llvm = llvmTypeForTypeName(elem_ty);
    if (!elem_llvm) {
      elem_llvm = i32Ty();
    }
    const ClassInfo* arr_info = findClass(scrut_type_name_);
    if (!arr_info) {
      error(pat.loc, "unknown Array type in pattern: '" + scrut_type_name_ + "'");
      return false;
    }
    auto dit = arr_info->field_index.find(physicalFieldName("data"));
    if (dit == arr_info->field_index.end()) {
      error(pat.loc, "Array layout missing 'data'");
      return false;
    }
    llvm::Value* data_gep = builder_.CreateStructGEP(arr_info->type, slot, dit->second,
                                                     "pat.arr.data");
    llvm::Value* data = builder_.CreateLoad(ptrTy(), data_gep, "pat.arr.base");
    for (size_t i = 0; i < pat.fields.size(); ++i) {
      const ast::PatternField& pf = pat.fields[i];
      const std::string bind = pf.binding.empty() ? ("_" + std::to_string(i)) : pf.binding;
      if (bind == "_") {
        continue;
      }
      llvm::Value* elem_ptr = builder_.CreateInBoundsGEP(
          elem_llvm, data, builder_.getInt32(static_cast<uint64_t>(i)), "pat.arr.p");
      llvm::AllocaInst* dst = createEntryAlloca(fn, elem_llvm, bind);
      builder_.CreateStore(builder_.CreateLoad(elem_llvm, elem_ptr, "pat.arr.v"), dst);
      bound.emplace_back(bind, dst);
      named_values_[bind] = dst;
      local_types_[bind] = elem_ty;
    }
    return true;
  }

  error(pat.loc, "unsupported destructuring pattern");
  return false;
}

void Codegen::restorePatternBindings(
    const std::vector<std::pair<std::string, llvm::Value*>>& bound,
    const std::unordered_map<std::string, std::string>& saved_types,
    const std::unordered_set<std::string>& saved_heap) {
  for (const auto& kv : bound) {
    named_values_.erase(kv.first);
    local_types_.erase(kv.first);
    heap_locals_.erase(kv.first);
  }
  for (const auto& kv : saved_types) {
    local_types_[kv.first] = kv.second;
  }
  for (const auto& n : saved_heap) {
    heap_locals_.insert(n);
  }
}

bool Codegen::emitMatch(const ast::MatchStmt& stmt) {
  // Desugar to if / else-if / else via icmp + br (evaluate scrutinee once).
  if (!stmt.scrutinee) {
    error(stmt.loc, "match missing expression");
    return false;
  }
  if (stmt.branches.empty()) {
    error(stmt.loc, "match requires at least one arm");
    return false;
  }

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  const std::string scrut_ty = inferTypeFromExpr(*stmt.scrutinee);
  llvm::Value* scrut_val = emitExpr(*stmt.scrutinee);
  if (!scrut_val) {
    return false;
  }

  llvm::Type* slot_ty = scrut_val->getType();
  if (scrut_ty == "String" || (isClassType(scrut_ty) && scrut_val->getType()->isPointerTy())) {
    slot_ty = ptrTy();
  } else if (scrut_ty == "bool") {
    slot_ty = i1Ty();
    scrut_val = toI1(scrut_val);
  } else if (!scrut_val->getType()->isPointerTy()) {
    slot_ty = i32Ty();
    scrut_val = toI32(scrut_val, stmt.loc);
  }
  llvm::AllocaInst* slot = createEntryAlloca(fn, slot_ty, "match.scrut");
  builder_.CreateStore(scrut_val, slot);

  llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(globalContext(), "match.merge", fn);
  bool saw_wildcard = false;

  for (size_t i = 0; i < stmt.branches.size(); ++i) {
    const ast::MatchBranch& br = stmt.branches[i];
    if (saw_wildcard) {
      error(stmt.loc, "unreachable match arm after '_'");
      return false;
    }

    if (br.is_wildcard) {
      saw_wildcard = true;
      if (!emitStmtListScoped(br.body)) {
        return false;
      }
      branchTo(merge_bb);
      continue;
    }

    // Destructuring arm: a class pattern is a runtime type test (TypeID), then
    // the body runs with each named field bound to a fresh local.
    if (br.pattern_kind == ast::MatchPatternKind::ClassDestructure ||
        br.pattern_kind == ast::MatchPatternKind::ArrayDestructure) {
      if (!br.destruct) {
        error(stmt.loc, "match arm missing destructuring pattern");
        return false;
      }
      scrut_type_name_ = scrut_ty;
      llvm::BasicBlock* then_bb =
          llvm::BasicBlock::Create(globalContext(), "match.destr", fn);
      llvm::BasicBlock* else_bb =
          llvm::BasicBlock::Create(globalContext(), "match.else", fn);
      if (br.pattern_kind == ast::MatchPatternKind::ArrayDestructure) {
        // `[a, b]` only matches when the array is long enough; longer arrays
        // still match (prefix pattern).
        std::string abase;
        std::vector<std::string> aargs;
        if (!splitGenericType(scrut_ty, abase, aargs) || abase != "Array") {
          error(stmt.loc, "array pattern requires an Array scrutinee, got '" + scrut_ty + "'");
          return false;
        }
        ensureTypeAvailable(scrut_ty);
        const ClassInfo* arr_info = findClass(scrut_ty);
        if (!arr_info) {
          error(stmt.loc, "unknown Array type in match: " + scrut_ty);
          return false;
        }
        auto lit = arr_info->field_index.find(physicalFieldName("length"));
        if (lit == arr_info->field_index.end()) {
          error(stmt.loc, "Array layout missing 'length'");
          return false;
        }
        llvm::Value* arr = builder_.CreateLoad(ptrTy(), slot, "match.arr.obj");
        llvm::Value* len_gep = builder_.CreateStructGEP(arr_info->type, arr, lit->second,
                                                        "match.arr.len.f");
        llvm::Value* len = builder_.CreateLoad(i32Ty(), len_gep, "match.arr.len");
        llvm::Value* need =
            llvm::ConstantInt::get(i32Ty(), static_cast<uint64_t>(br.destruct->fields.size()));
        llvm::Value* ok = builder_.CreateICmpSGE(len, need, "match.arr.ok");
        builder_.CreateCondBr(ok, then_bb, else_bb);
        builder_.SetInsertPoint(then_bb);
        std::vector<std::pair<std::string, llvm::Value*>> bound;
        std::unordered_map<std::string, std::string> saved_types;
        std::unordered_set<std::string> saved_heap;
        for (const ast::PatternField& pf : br.destruct->fields) {
          if (pf.binding.empty() || pf.binding == "_") {
            continue;
          }
          auto it = named_values_.find(pf.binding);
          if (it != named_values_.end()) {
            bound.emplace_back(pf.binding, it->second);
          }
          auto ti = local_types_.find(pf.binding);
          if (ti != local_types_.end()) {
            saved_types[pf.binding] = ti->second;
          }
          if (heap_locals_.count(pf.binding) != 0) {
            saved_heap.insert(pf.binding);
          }
        }
        if (!emitPatternBindings(*br.destruct, arr, bound)) {
          return false;
        }
        if (!emitStmtListScoped(br.body)) {
          return false;
        }
        restorePatternBindings(bound, saved_types, saved_heap);
        branchTo(merge_bb);
        builder_.SetInsertPoint(else_bb);
        continue;
      }
      const std::string want = resolveTypeName(br.destruct->class_name);
      if (!ensureTypeAvailable(want) || !isClassType(want)) {
        error(stmt.loc, "destructuring pattern requires a class type, got '" +
                          br.destruct->class_name + "'");
        return false;
      }
      if (resolveTypeName(scrut_ty) != want) {
        error(stmt.loc, "match arm pattern '" + br.destruct->class_name +
                          "' cannot match scrutinee of type '" + scrut_ty + "'");
        return false;
      }
      builder_.CreateBr(then_bb);

      builder_.SetInsertPoint(then_bb);
      // Save bindings that the arm shadows so they come back after the body.
      std::vector<std::pair<std::string, llvm::Value*>> bound;
      std::unordered_map<std::string, std::string> saved_types;
      std::unordered_set<std::string> saved_heap;
      for (const ast::PatternField& pf : br.destruct->fields) {
        const std::string n = pf.binding.empty() ? pf.name : pf.binding;
        if (n.empty() || n == "_") {
          continue;
        }
        auto it = named_values_.find(n);
        if (it != named_values_.end()) {
          bound.emplace_back(n, it->second);
        }
        auto ti = local_types_.find(n);
        if (ti != local_types_.end()) {
          saved_types[n] = ti->second;
        }
        if (heap_locals_.count(n) != 0) {
          saved_heap.insert(n);
        }
      }
      // `slot` holds the object pointer for class scrutinees; patterns GEP the object.
      llvm::Value* obj_ptr = builder_.CreateLoad(ptrTy(), slot, "match.obj");
      if (!emitPatternBindings(*br.destruct, obj_ptr, bound)) {
        return false;
      }
      if (!emitStmtListScoped(br.body)) {
        return false;
      }
      restorePatternBindings(bound, saved_types, saved_heap);
      branchTo(merge_bb);
      // A class pattern is not exhaustive on its own: following arms stay reachable.
      builder_.SetInsertPoint(else_bb);
      continue;
    }

    if (!br.pattern && br.pattern_kind != ast::MatchPatternKind::Some &&
        br.pattern_kind != ast::MatchPatternKind::None) {
      error(stmt.loc, "match arm missing pattern");
      return false;
    }

    // `none => { ... }` terminates the chain: no Optional value can reach here.
    if (br.pattern_kind == ast::MatchPatternKind::None) {
      if (scrut_ty.rfind("Optional<", 0) != 0) {
        error(stmt.loc, "'none' pattern requires an Optional scrutinee, got '" + scrut_ty + "'");
        return false;
      }
      saw_wildcard = true;
      if (!emitStmtListScoped(br.body)) {
        return false;
      }
      branchTo(merge_bb);
      continue;
    }

    // `some(x) => { ... }` in statement form: test has_value, bind the payload.
    if (br.pattern_kind == ast::MatchPatternKind::Some) {
      if (scrut_ty.rfind("Optional<", 0) != 0) {
        error(stmt.loc, "'some(...)' pattern requires an Optional scrutinee, got '" + scrut_ty + "'");
        return false;
      }
      ensureTypeAvailable(scrut_ty);
      const ClassInfo* opt_info = findClass(scrut_ty);
      if (!opt_info || !opt_info->type) {
        error(stmt.loc, "unknown Optional type in match: " + scrut_ty);
        return false;
      }
      auto hv = opt_info->field_index.find(physicalFieldName("has_value"));
      auto vi = opt_info->field_index.find(physicalFieldName("value"));
      if (hv == opt_info->field_index.end() || vi == opt_info->field_index.end()) {
        error(stmt.loc, "Optional layout missing 'has_value'/'value'");
        return false;
      }
      llvm::BasicBlock* some_bb =
          llvm::BasicBlock::Create(globalContext(), "match.some", fn);
      llvm::BasicBlock* skip_bb =
          llvm::BasicBlock::Create(globalContext(), "match.else", fn);
      // Optional is a plain class, so `slot` holds its object pointer; load the
      // has_value field through a GEP when so.
      llvm::Value* base = slot;
      if (opt_info->type && slot->getType() != ptrTy()) {
        base = slot;
      } else {
        base = builder_.CreateLoad(ptrTy(), slot, "match.opt.obj");
      }
      llvm::Value* hv_gep = builder_.CreateStructGEP(
          opt_info->type, base, static_cast<unsigned>(hv->second), "match.has.f");
      llvm::Value* has = builder_.CreateLoad(i1Ty(), hv_gep, "match.has");
      builder_.CreateCondBr(has, some_bb, skip_bb);
      builder_.SetInsertPoint(some_bb);
      if (br.binding.empty() || br.binding == "_") {
        if (!emitStmtListScoped(br.body)) {
          return false;
        }
        branchTo(merge_bb);
        builder_.SetInsertPoint(skip_bb);
        continue;
      }
      llvm::Value* payload_gep = builder_.CreateStructGEP(
          opt_info->type, base, static_cast<unsigned>(vi->second), "match.payload.f");
      const std::string inner = scrut_ty.substr(9, scrut_ty.size() - 10);
      llvm::Type* payload_ty = llvmTypeForTypeName(inner);
      if (!payload_ty) {
        payload_ty = llvm::cast<llvm::StructType>(opt_info->type)->getElementType(
            static_cast<unsigned>(vi->second));
      }
      llvm::AllocaInst* binding_slot = createEntryAlloca(fn, payload_ty, br.binding);
      builder_.CreateStore(builder_.CreateLoad(payload_ty, payload_gep, "match.payload"),
                           binding_slot);
      const bool prev = named_values_.count(br.binding) != 0;
      llvm::Value* prev_slot = prev ? named_values_[br.binding] : nullptr;
      const bool had_type = local_types_.count(br.binding) != 0;
      std::string prev_type = had_type ? local_types_[br.binding] : std::string();
      named_values_[br.binding] = binding_slot;
      local_types_[br.binding] = inner;
      const bool body_ok = emitStmtListScoped(br.body);
      if (prev) {
        named_values_[br.binding] = prev_slot;
      } else {
        named_values_.erase(br.binding);
      }
      if (had_type) {
        local_types_[br.binding] = prev_type;
      } else {
        local_types_.erase(br.binding);
      }
      if (!body_ok) {
        return false;
      }
      branchTo(merge_bb);
      builder_.SetInsertPoint(skip_bb);
      continue;
    }

    llvm::BasicBlock* then_bb =
        llvm::BasicBlock::Create(globalContext(), "match.then", fn);
    llvm::BasicBlock* else_bb =
        llvm::BasicBlock::Create(globalContext(), "match.else", fn);

    llvm::Value* loaded = builder_.CreateLoad(slot_ty, slot, "match.v");
    llvm::Value* cond = nullptr;
    if (br.pattern->kind() == ast::ExprKind::IntLiteral) {
      llvm::Value* pat = toI32(emitExpr(*br.pattern), br.loc);
      if (!pat) {
        return false;
      }
      cond = builder_.CreateICmpEQ(toI32(loaded, br.loc), pat, "match.eq");
    } else if (br.pattern->kind() == ast::ExprKind::BoolLiteral) {
      llvm::Value* pat = toI1(emitExpr(*br.pattern));
      if (!pat) {
        return false;
      }
      cond = builder_.CreateICmpEQ(toI1(loaded), pat, "match.eq");
    } else if (br.pattern->kind() == ast::ExprKind::StringLiteral) {
      llvm::Function* str_eq = requireExtern("l_str_eq", br.loc);
      if (!str_eq) {
        return false;
      }
      llvm::Value* lhs = loaded;
      if (scrut_ty == "String") {
        ensureTypeAvailable("String");
        const ClassInfo* info = findClass("String");
        if (!info) {
          error(stmt.loc, "String type not available for match");
          return false;
        }
        auto fit = info->field_index.find("data_");
        if (fit == info->field_index.end()) {
          error(stmt.loc, "String.data_ not found");
          return false;
        }
        llvm::Value* gep =
            builder_.CreateStructGEP(info->type, loaded, fit->second, "match.str.data");
        lhs = builder_.CreateLoad(ptrTy(), gep, "match.str.ptr");
      }
      llvm::Value* rhs = emitExpr(*br.pattern);
      if (!lhs || !rhs) {
        return false;
      }
      llvm::Value* eq_i32 = builder_.CreateCall(str_eq, {lhs, rhs}, "match.streq");
      cond = builder_.CreateICmpNE(eq_i32, llvm::ConstantInt::get(i32Ty(), 0), "match.eq");
    } else {
      error(stmt.loc, "match patterns must be literals or '_'");
      return false;
    }

    builder_.CreateCondBr(cond, then_bb, else_bb);

    builder_.SetInsertPoint(then_bb);
    if (!emitStmtListScoped(br.body)) {
      return false;
    }
    branchTo(merge_bb);

    builder_.SetInsertPoint(else_bb);
  }

  // Fallthrough when no '_' arm: empty else chain ends here.
  branchTo(merge_bb);
  builder_.SetInsertPoint(merge_bb);
  return true;
}

llvm::Value* Codegen::emitMatchExpr(const ast::MatchExpr& expr) {
  if (!expr.scrutinee) {
    error(expr.loc, "match missing expression");
    return nullptr;
  }
  if (expr.branches.empty()) {
    error(expr.loc, "match requires at least one arm");
    return nullptr;
  }
  const ast::SourceLocation scratch_expr_loc = expr.loc;

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  const std::string scrut_ty = exprTypeOf(*expr.scrutinee);
  const bool is_opt = scrut_ty.rfind("Optional<", 0) == 0;

  // Evaluate the scrutinee exactly once and materialize it in an entry slot.
  llvm::Value* scrut_val = emitExpr(*expr.scrutinee);
  if (!scrut_val) {
    return nullptr;
  }
  llvm::Type* slot_ty = scrut_val->getType();
  if (is_opt) {
    ensureTypeAvailable(scrut_ty);
    const ClassInfo* opt_info = findClass(scrut_ty);
    if (!opt_info || !opt_info->type) {
      error(expr.loc, "unknown Optional type in match: " + scrut_ty);
      return nullptr;
    }
    slot_ty = opt_info->type;
    if (scrut_val->getType()->isPointerTy()) {
      scrut_val = builder_.CreateLoad(opt_info->type, scrut_val, "match.opt");
    }
  } else if (scrut_ty == "String" ||
             (isClassType(scrut_ty) && scrut_val->getType()->isPointerTy())) {
    slot_ty = ptrTy();
  } else if (scrut_ty == "bool") {
    slot_ty = i1Ty();
    scrut_val = toI1(scrut_val);
  } else if (!scrut_val->getType()->isPointerTy()) {
    slot_ty = i32Ty();
    scrut_val = toI32(scrut_val, expr.loc);
  }
  llvm::AllocaInst* slot = createEntryAlloca(fn, slot_ty, "match.scrut");
  builder_.CreateStore(scrut_val, slot);

  llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(globalContext(), "matchx.merge", fn);
  std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> incoming;
  llvm::Type* result_ty = nullptr;

  auto arm_matched = [&](llvm::Value* v, const ast::Expr* src) -> bool {
    if (!v) {
      return false;
    }
    // String literals arrive as raw `ptr`; normalize to the String object pointer
    // (same representation `??` uses) so every arm shares one PHI type.
    if (src && src->kind() == ast::ExprKind::StringLiteral && v->getType()->isPointerTy()) {
      llvm::Value* obj = emitStringObjectPtr(*src);
      if (!obj) {
        return false;
      }
      v = obj;
    }
    llvm::BasicBlock* end_bb = builder_.GetInsertBlock();
    if (!result_ty) {
      result_ty = v->getType();
    } else if (result_ty != v->getType()) {
      std::string want_desc;
      std::string got_desc;
      llvm::raw_string_ostream wo(want_desc);
      llvm::raw_string_ostream go(got_desc);
      result_ty->print(wo);
      v->getType()->print(go);
      error(expr.loc, "match arms have mismatched types: expected " + wo.str() + ", got " +
                          go.str());
      return false;
    }
    incoming.emplace_back(v, end_bb);
    builder_.CreateBr(merge_bb);
    return true;
  };

  bool chain_terminated = false;  // a `_` / `none` arm ended the chain
  for (size_t i = 0; i < expr.branches.size(); ++i) {
    const ast::MatchBranch& br = expr.branches[i];
    if (br.pattern_kind == ast::MatchPatternKind::Wildcard ||
        br.pattern_kind == ast::MatchPatternKind::None) {
      // `_` and `none` both terminate the chain: every following arm is dead.
      if (i + 1 < expr.branches.size()) {
        error(expr.branches[i + 1].loc, "unreachable match arm after exhaustive pattern");
        return nullptr;
      }
      if (br.pattern_kind == ast::MatchPatternKind::None && !is_opt) {
        error(expr.loc, "'none' pattern requires an Optional scrutinee, got '" + scrut_ty + "'");
        return nullptr;
      }
      if (!br.value) {
        error(expr.loc, "match arm must yield an expression");
        return nullptr;
      }
      llvm::Value* v = emitExpr(*br.value);
      if (!v) {
        return nullptr;
      }
      if (!arm_matched(v, br.value.get())) {
        return nullptr;
      }
      chain_terminated = true;
      break;
    }

    llvm::BasicBlock* then_bb =
        llvm::BasicBlock::Create(globalContext(), "matchx.arm", fn);
    llvm::BasicBlock* else_bb =
        llvm::BasicBlock::Create(globalContext(), "matchx.next", fn);
    llvm::Value* cond = nullptr;
    if (br.pattern_kind == ast::MatchPatternKind::Some) {
      if (!is_opt) {
        error(expr.loc, "'some(...)' pattern requires an Optional scrutinee, got '" + scrut_ty +
                          "'");
        return nullptr;
      }
      const ClassInfo* opt_info = findClass(scrut_ty);
      auto hv = opt_info->field_index.find(physicalFieldName("has_value"));
      if (hv == opt_info->field_index.end()) {
        error(expr.loc, "Optional layout missing 'has_value'");
        return nullptr;
      }
      llvm::Value* loaded = builder_.CreateLoad(opt_info->type, slot, "match.opt.v");
      cond = builder_.CreateExtractValue(loaded, static_cast<unsigned>(hv->second),
                                         "match.has");

      // Bind the payload inside the arm block.
      builder_.CreateCondBr(cond, then_bb, else_bb);
      builder_.SetInsertPoint(then_bb);
      auto vi = opt_info->field_index.find(physicalFieldName("value"));
      if (vi == opt_info->field_index.end()) {
        error(expr.loc, "Optional layout missing 'value'");
        return nullptr;
      }
      if (br.binding.empty()) {
        error(expr.loc, "'some' pattern requires a binding name");
        return nullptr;
      }
      llvm::Value* payload_gep =
          builder_.CreateInBoundsGEP(opt_info->type, slot,
                                     {builder_.getInt32(0),
                                      builder_.getInt32(static_cast<uint64_t>(vi->second))},
                                     br.binding + ".slot");
      // Payload type name: the T inside Optional<T>.
      const std::string inner = scrut_ty.substr(9, scrut_ty.size() - 10);
      llvm::Type* payload_ty = llvmTypeForTypeName(inner);
      if (!payload_ty) {
        payload_ty = llvm::cast<llvm::StructType>(opt_info->type)->getElementType(
            static_cast<unsigned>(vi->second));
      }
      // Fresh slot so the binding behaves like an ordinary local.
      llvm::AllocaInst* binding_slot = createEntryAlloca(fn, payload_ty, br.binding);
      builder_.CreateStore(builder_.CreateLoad(payload_ty, payload_gep, br.binding + ".val"),
                           binding_slot);
      const bool prev = named_values_.count(br.binding) != 0;
      llvm::Value* prev_slot = prev ? named_values_[br.binding] : nullptr;
      const bool had_type = local_types_.count(br.binding) != 0;
      std::string prev_type = had_type ? local_types_[br.binding] : std::string();
      named_values_[br.binding] = binding_slot;
      local_types_[br.binding] = inner;
      llvm::Value* v = emitExpr(*br.value);
      if (prev) {
        named_values_[br.binding] = prev_slot;
      } else {
        named_values_.erase(br.binding);
      }
      if (had_type) {
        local_types_[br.binding] = prev_type;
      } else {
        local_types_.erase(br.binding);
      }
      if (!v) {
        return nullptr;
      }
      if (!arm_matched(v, br.value.get())) {
        return nullptr;
      }
      builder_.SetInsertPoint(else_bb);
      continue;
    }

    if (!br.pattern && br.pattern_kind != ast::MatchPatternKind::ClassDestructure &&
        br.pattern_kind != ast::MatchPatternKind::ArrayDestructure) {
      error(expr.loc, "match arm missing pattern");
      return nullptr;
    }

    // Destructuring arm in expression form: `User { name } => name`.
    if (br.pattern_kind == ast::MatchPatternKind::ClassDestructure ||
        br.pattern_kind == ast::MatchPatternKind::ArrayDestructure) {
      if (!br.destruct) {
        error(expr.loc, "match arm missing destructuring pattern");
        return nullptr;
      }
      scrut_type_name_ = scrut_ty;
      if (br.pattern_kind == ast::MatchPatternKind::ArrayDestructure) {
        // `[a, b] => expr` — prefix match on an Array<T> (length >= N).
        std::string abase;
        std::vector<std::string> aargs;
        if (!splitGenericType(scrut_ty, abase, aargs) || abase != "Array") {
          error(expr.loc, "array pattern requires an Array scrutinee, got '" + scrut_ty + "'");
          return nullptr;
        }
        ensureTypeAvailable(scrut_ty);
        const ClassInfo* arr_info = findClass(scrut_ty);
        if (!arr_info) {
          error(expr.loc, "unknown Array type in match: " + scrut_ty);
          return nullptr;
        }
        auto lit = arr_info->field_index.find(physicalFieldName("length"));
        if (lit == arr_info->field_index.end()) {
          error(expr.loc, "Array layout missing 'length'");
          return nullptr;
        }
        llvm::Value* arr = builder_.CreateLoad(ptrTy(), slot, "matchx.arr.obj");
        llvm::Value* len_gep = builder_.CreateStructGEP(arr_info->type, arr, lit->second,
                                                        "matchx.arr.len.f");
        llvm::Value* len = builder_.CreateLoad(i32Ty(), len_gep, "matchx.arr.len");
        llvm::Value* need =
            llvm::ConstantInt::get(i32Ty(), static_cast<uint64_t>(br.destruct->fields.size()));
        llvm::Value* ok = builder_.CreateICmpSGE(len, need, "matchx.arr.ok");
        builder_.CreateCondBr(ok, then_bb, else_bb);
        builder_.SetInsertPoint(then_bb);
        std::vector<std::pair<std::string, llvm::Value*>> bound;
        std::unordered_map<std::string, std::string> saved_types;
        std::unordered_set<std::string> saved_heap;
        for (const ast::PatternField& pf : br.destruct->fields) {
          if (pf.binding.empty() || pf.binding == "_") {
            continue;
          }
          auto it = named_values_.find(pf.binding);
          if (it != named_values_.end()) {
            bound.emplace_back(pf.binding, it->second);
          }
          auto ti = local_types_.find(pf.binding);
          if (ti != local_types_.end()) {
            saved_types[pf.binding] = ti->second;
          }
          if (heap_locals_.count(pf.binding) != 0) {
            saved_heap.insert(pf.binding);
          }
        }
        if (!emitPatternBindings(*br.destruct, arr, bound)) {
          return nullptr;
        }
        if (!br.value) {
          error(expr.loc, "match arm must yield an expression");
          return nullptr;
        }
        llvm::Value* v = emitExpr(*br.value);
        if (!v) {
          return nullptr;
        }
        if (!arm_matched(v, br.value.get())) {
          return nullptr;
        }
        restorePatternBindings(bound, saved_types, saved_heap);
        builder_.SetInsertPoint(else_bb);
        continue;
      }
      const std::string want = resolveTypeName(br.destruct->class_name);
      if (!ensureTypeAvailable(want) || !isClassType(want)) {
        error(expr.loc, "destructuring pattern requires a class type, got '" +
                          br.destruct->class_name + "'");
        return nullptr;
      }
      if (resolveTypeName(scrut_ty) != want) {
        error(expr.loc, "match arm pattern '" + br.destruct->class_name +
                          "' cannot match scrutinee of type '" + scrut_ty + "'");
        return nullptr;
      }
      // Class pattern: not a runtime test on a typed scrutinee, so jump straight
      // into the arm block (`then_bb`, which binds fields) and keep `else_bb`
      // as the fall-through of the remaining arms.
      builder_.CreateBr(then_bb);
      builder_.SetInsertPoint(then_bb);

      std::vector<std::pair<std::string, llvm::Value*>> bound;
      std::unordered_map<std::string, std::string> saved_types;
      std::unordered_set<std::string> saved_heap;
      for (const ast::PatternField& pf : br.destruct->fields) {
        const std::string n = pf.binding.empty() ? pf.name : pf.binding;
        if (n.empty() || n == "_") {
          continue;
        }
        auto it = named_values_.find(n);
        if (it != named_values_.end()) {
          bound.emplace_back(n, it->second);
        }
        auto ti = local_types_.find(n);
        if (ti != local_types_.end()) {
          saved_types[n] = ti->second;
        }
        if (heap_locals_.count(n) != 0) {
          saved_heap.insert(n);
        }
      }
      llvm::Value* obj_ptr = builder_.CreateLoad(ptrTy(), slot, "matchx.obj");
      if (!emitPatternBindings(*br.destruct, obj_ptr, bound)) {
        return nullptr;
      }
      if (!br.value) {
        error(expr.loc, "match arm must yield an expression");
        return nullptr;
      }
      llvm::Value* v = emitExpr(*br.value);
      if (!v) {
        return nullptr;
      }
      if (!arm_matched(v, br.value.get())) {
        return nullptr;
      }
      restorePatternBindings(bound, saved_types, saved_heap);
      builder_.SetInsertPoint(else_bb);
      continue;
    }

    llvm::Value* loaded = builder_.CreateLoad(slot_ty, slot, "match.v");
    if (br.pattern->kind() == ast::ExprKind::IntLiteral) {
      llvm::Value* pat = toI32(emitExpr(*br.pattern), br.loc);
      if (!pat) {
        return nullptr;
      }
      cond = builder_.CreateICmpEQ(toI32(loaded, br.loc), pat, "match.eq");
    } else if (br.pattern->kind() == ast::ExprKind::BoolLiteral) {
      llvm::Value* pat = toI1(emitExpr(*br.pattern));
      if (!pat) {
        return nullptr;
      }
      cond = builder_.CreateICmpEQ(toI1(loaded), pat, "match.eq");
    } else if (br.pattern->kind() == ast::ExprKind::StringLiteral) {
      llvm::Function* str_eq = requireExtern("l_str_eq", br.loc);
      if (!str_eq) {
        return nullptr;
      }
      llvm::Value* lhs = loaded;
      if (scrut_ty == "String") {
        ensureTypeAvailable("String");
        const ClassInfo* info = findClass("String");
        auto fit = info->field_index.find("data_");
        if (fit == info->field_index.end()) {
          error(expr.loc, "String.data_ not found");
          return nullptr;
        }
        llvm::Value* gep =
            builder_.CreateStructGEP(info->type, loaded, fit->second, "match.str.data");
        lhs = builder_.CreateLoad(ptrTy(), gep, "match.str.ptr");
      }
      llvm::Value* rhs = emitExpr(*br.pattern);
      if (!lhs || !rhs) {
        return nullptr;
      }
      llvm::Value* eq_i32 = builder_.CreateCall(str_eq, {lhs, rhs}, "match.streq");
      cond = builder_.CreateICmpNE(eq_i32, llvm::ConstantInt::get(i32Ty(), 0), "match.eq");
    } else {
      error(expr.loc, "match patterns must be literals, '_', 'some(...)' or 'none'");
      return nullptr;
    }

    builder_.CreateCondBr(cond, then_bb, else_bb);
    builder_.SetInsertPoint(then_bb);
    if (!br.value) {
      error(expr.loc, "match arm must yield an expression");
      return nullptr;
    }
    llvm::Value* v = emitExpr(*br.value);
    if (!v) {
      return nullptr;
    }
    if (!arm_matched(v, br.value.get())) {
      return nullptr;
    }
    builder_.SetInsertPoint(else_bb);
  }

  if (!result_ty) {
    error(expr.loc, "match has no value-producing arm");
    return nullptr;
  }
  // Unmatched fallthrough (every arm tested and none matched) merges a typed
  // placeholder so the PHI stays well-formed. A terminating arm already jumped.
  if (!chain_terminated) {
    llvm::Value* fallback = llvm::Constant::getNullValue(result_ty);
    incoming.emplace_back(fallback, builder_.GetInsertBlock());
    builder_.CreateBr(merge_bb);
  }

  builder_.SetInsertPoint(merge_bb);
  llvm::PHINode* phi = builder_.CreatePHI(result_ty, static_cast<unsigned>(incoming.size()),
                                          "match.val");
  for (const auto& inc : incoming) {
    phi->addIncoming(inc.first, inc.second);
  }
  return phi;
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
  loop_stack_.push_back(LoopTargets{cond_bb, end_bb, defer_scopes_.size()});
  const bool body_ok = emitStmtListScoped(stmt.body);
  loop_stack_.pop_back();
  if (!body_ok) {
    return false;
  }
  // `break` already terminates the block; only branch back when still open.
  branchTo(cond_bb);

  builder_.SetInsertPoint(end_bb);
  return true;
}

bool Codegen::emitBreak(const ast::BreakStmt& stmt) {
  (void)stmt;
  if (loop_stack_.empty()) {
    error(stmt.loc, "'break' outside a loop");
    return false;
  }
  flushLoopJumpScopes();
  branchTo(loop_stack_.back().end_bb);
  return true;
}

bool Codegen::emitContinue(const ast::ContinueStmt& stmt) {
  (void)stmt;
  if (loop_stack_.empty()) {
    error(stmt.loc, "'continue' outside a loop");
    return false;
  }
  flushLoopJumpScopes();
  branchTo(loop_stack_.back().cond_bb);
  return true;
}

// Flush `defer` bodies and auto-frees for blocks we are jumping out of.
// Only instrumentation is emitted; the scope stacks are left untouched and are
// unwound by the enclosing emitStmtListScoped / loop epilogue. flushCurrent*
// clears the entries it emits, so nothing can be double-freed.
void Codegen::flushLoopJumpScopes() {
  if (loop_stack_.empty()) {
    return;
  }
  const size_t base = loop_stack_.back().base_scope_depth;
  if (defer_scopes_.size() <= base) {
    return;
  }
  // Emit from the innermost scope outwards by temporarily walking a copy of the
  // tail, without mutating the stack depth.
  const size_t extra = defer_scopes_.size() - base;
  for (size_t k = 0; k < extra; ++k) {
    const size_t idx = defer_scopes_.size() - 1 - k;
    auto& scope = defer_scopes_[idx];
    for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
      const ast::DeferStmt* def = *it;
      if (!def) {
        continue;
      }
      for (const ast::StmtPtr& s : def->body) {
        if (s && !emitStmt(*s)) {
          return;
        }
      }
    }
    scope.clear();
    if (idx < auto_free_scopes_.size()) {
      auto_free_scopes_[idx].clear();
    }
  }
}

bool Codegen::emitForInRange(const ast::ForInStmt& stmt, const ast::BinaryExpr& range) {
  // Desugar: for i in lo..hi { body }  (exclusive end, like Rust)
  //   → i32 i = lo
  //     while i < hi { body; i = i + 1 }
  if (!range.left || !range.right) {
    error(stmt.loc, "range for-in missing bounds");
    return false;
  }
  if (named_values_.count(stmt.item_name) != 0) {
    error(stmt.loc, "for-in loop variable shadows existing name: " + stmt.item_name);
    return false;
  }

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* item_slot = createEntryAlloca(fn, i32Ty(), stmt.item_name);
  llvm::AllocaInst* end_slot = createEntryAlloca(fn, i32Ty(), stmt.item_name + ".end");
  llvm::Value* start = toI32(emitExpr(*range.left), range.loc);
  llvm::Value* end = toI32(emitExpr(*range.right), range.loc);
  if (!start || !end) {
    return false;
  }
  builder_.CreateStore(start, item_slot);
  builder_.CreateStore(end, end_slot);

  llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(globalContext(), "for.range.cond", fn);
  llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(globalContext(), "for.range.body", fn);
  llvm::BasicBlock* inc_bb = llvm::BasicBlock::Create(globalContext(), "for.range.inc", fn);
  llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(globalContext(), "for.range.end", fn);

  branchTo(cond_bb);
  builder_.SetInsertPoint(cond_bb);
  llvm::Value* cur = builder_.CreateLoad(i32Ty(), item_slot, "for.range.i");
  llvm::Value* end_now = builder_.CreateLoad(i32Ty(), end_slot, "for.range.end");
  llvm::Value* cmp = builder_.CreateICmpSLT(cur, end_now, "for.range.cmp");
  builder_.CreateCondBr(cmp, body_bb, end_bb);

  builder_.SetInsertPoint(body_bb);
  auto saved_named = named_values_;
  auto saved_locals = local_types_;
  auto saved_heap = heap_locals_;
  auto saved_owned_strings = owned_string_locals_;
  named_values_[stmt.item_name] = item_slot;
  local_types_[stmt.item_name] = "i32";
  heap_locals_.erase(stmt.item_name);

  enterDeferScope();
  // `continue` must run the increment, not jump straight back to the condition.
  loop_stack_.push_back(LoopTargets{inc_bb, end_bb, defer_scopes_.size()});
  const bool body_ok = emitStmtList(stmt.body);
  loop_stack_.pop_back();
  if (body_ok) {
    exitDeferScope();
  } else {
    if (!defer_scopes_.empty()) {
      defer_scopes_.pop_back();
    }
    if (!auto_free_scopes_.empty()) {
      auto_free_scopes_.pop_back();
    }
  }

  named_values_ = std::move(saved_named);
  local_types_ = std::move(saved_locals);
  heap_locals_ = std::move(saved_heap);
  owned_string_locals_ = std::move(saved_owned_strings);

  if (!body_ok) {
    return false;
  }

  branchTo(inc_bb);
  builder_.SetInsertPoint(inc_bb);
  {
    llvm::Value* v = builder_.CreateLoad(i32Ty(), item_slot, "for.range.i.inc");
    llvm::Value* next = builder_.CreateAdd(v, llvm::ConstantInt::get(i32Ty(), 1), "for.range.next");
    builder_.CreateStore(next, item_slot);
    branchTo(cond_bb);
  }

  builder_.SetInsertPoint(end_bb);
  return true;
}

bool Codegen::emitForIn(const ast::ForInStmt& stmt) {
  // Desugar: for item in arr { body }
  //   → i32 __for_i = 0
  //     while __for_i < arr.length_ {
  //       T item = arr.get(__for_i)
  //       body
  //       __for_i = __for_i + 1
  //     }
  if (!stmt.iterable) {
    error(stmt.loc, "for-in missing iterable");
    return false;
  }
  if (stmt.iterable->kind() == ast::ExprKind::Binary) {
    const auto& bin = static_cast<const ast::BinaryExpr&>(*stmt.iterable);
    if (bin.op == "..") {
      return emitForInRange(stmt, bin);
    }
  }
  // Iterable may be a local (`arr`) or a member access (`this.plugins`, `obj.plugins`).
  std::string arr_ty;
  if (stmt.iterable->kind() == ast::ExprKind::Identifier) {
    const auto& arr_id = static_cast<const ast::IdentifierExpr&>(*stmt.iterable);
    auto lit = local_types_.find(arr_id.name);
    if (lit == local_types_.end()) {
      error(stmt.loc, "unknown iterable in for-in: " + arr_id.name);
      return false;
    }
    arr_ty = lit->second;
  } else if (stmt.iterable->kind() == ast::ExprKind::MemberAccess) {
    arr_ty = exprTypeOf(*stmt.iterable);
    if (arr_ty.empty()) {
      arr_ty = inferTypeFromExpr(*stmt.iterable);
    }
    if (arr_ty.empty()) {
      error(stmt.loc, "cannot infer type of for-in iterable");
      return false;
    }
  } else {
    error(stmt.loc, "for-in iterable must be a local Array, a field, or a lo..hi range");
    return false;
  }
  ensureTypeAvailable(arr_ty);
  const ClassInfo* info = findClass(arr_ty);
  if (!info || info->type_args.empty() || info->template_name != "Array") {
    error(stmt.loc, "for-in requires Array<T>, got " + arr_ty);
    return false;
  }
  auto len_fit = info->field_index.find("length_");
  auto get_it = info->methods.find("get");
  if (len_fit == info->field_index.end() || get_it == info->methods.end()) {
    error(stmt.loc, "Array missing length_ / get for for-in lowering");
    return false;
  }
  if (named_values_.count(stmt.item_name) != 0) {
    error(stmt.loc, "for-in loop variable shadows existing name: " + stmt.item_name);
    return false;
  }

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  const std::string elem_ty_name = info->type_args[0];
  llvm::Type* elem_ty = llvmTypeForTypeName(elem_ty_name);

  // Hidden index alloca (not a user-visible name).
  static unsigned for_seq = 0;
  const std::string idx_name = "__for_i_" + std::to_string(for_seq++);
  llvm::AllocaInst* idx_slot = createEntryAlloca(fn, i32Ty(), idx_name);
  builder_.CreateStore(llvm::ConstantInt::get(i32Ty(), 0), idx_slot);

  llvm::AllocaInst* item_slot = createEntryAlloca(fn, elem_ty, stmt.item_name);

  llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(globalContext(), "for.cond", fn);
  llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(globalContext(), "for.body", fn);
  llvm::BasicBlock* inc_bb = llvm::BasicBlock::Create(globalContext(), "for.inc", fn);
  llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(globalContext(), "for.end", fn);

  branchTo(cond_bb);
  builder_.SetInsertPoint(cond_bb);
  llvm::Value* arr_ptr = classValuePtr(*stmt.iterable, arr_ty);
  if (!arr_ptr) {
    return false;
  }
  llvm::Value* len_gep =
      builder_.CreateStructGEP(info->type, arr_ptr, len_fit->second, "for.len.ptr");
  llvm::Value* len = builder_.CreateLoad(i32Ty(), len_gep, "for.len");
  llvm::Value* idx = builder_.CreateLoad(i32Ty(), idx_slot, "for.i");
  llvm::Value* cmp = builder_.CreateICmpSLT(idx, len, "for.cmp");
  builder_.CreateCondBr(cmp, body_bb, end_bb);

  builder_.SetInsertPoint(body_bb);
  // Refresh arr/idx for body (SSA across blocks).
  arr_ptr = classValuePtr(*stmt.iterable, arr_ty);
  idx = builder_.CreateLoad(i32Ty(), idx_slot, "for.i.body");
  llvm::Value* elem = builder_.CreateCall(get_it->second, {arr_ptr, idx}, "for.elem");
  builder_.CreateStore(elem, item_slot);

  // Bind loop variable for the body scope only.
  auto saved_named = named_values_;
  auto saved_locals = local_types_;
  auto saved_heap = heap_locals_;
  auto saved_owned_strings = owned_string_locals_;
  named_values_[stmt.item_name] = item_slot;
  local_types_[stmt.item_name] = elem_ty_name;
  heap_locals_.erase(stmt.item_name);

  enterDeferScope();
  // `continue` must run the index increment before re-testing.
  loop_stack_.push_back(LoopTargets{inc_bb, end_bb, defer_scopes_.size()});
  const bool body_ok = emitStmtList(stmt.body);
  loop_stack_.pop_back();
  if (body_ok) {
    exitDeferScope();
  } else {
    if (!defer_scopes_.empty()) {
      defer_scopes_.pop_back();
    }
    if (!auto_free_scopes_.empty()) {
      auto_free_scopes_.pop_back();
    }
  }

  named_values_ = std::move(saved_named);
  local_types_ = std::move(saved_locals);
  heap_locals_ = std::move(saved_heap);
  owned_string_locals_ = std::move(saved_owned_strings);

  if (!body_ok) {
    return false;
  }

  branchTo(inc_bb);
  builder_.SetInsertPoint(inc_bb);
  {
    llvm::Value* cur = builder_.CreateLoad(i32Ty(), idx_slot, "for.i.inc");
    llvm::Value* next = builder_.CreateAdd(cur, llvm::ConstantInt::get(i32Ty(), 1), "for.i.next");
    builder_.CreateStore(next, idx_slot);
    branchTo(cond_bb);
  }

  builder_.SetInsertPoint(end_bb);
  return true;
}

llvm::Value* Codegen::emitStringObjectPtr(const ast::Expr& expr) {
  // Returns a pointer to a String object (suitable as concat's this*).
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error(expr.loc, "String type not available");
    return nullptr;
  }
  if (expr.kind() == ast::ExprKind::StringLiteral) {
    auto init_it = info->methods.find("init");
    if (init_it == info->methods.end()) {
      error(expr.loc, "String.init not found");
      return nullptr;
    }
    llvm::Function* cur = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, "str.lit");
    for (size_t i = 0; i < info->field_types.size(); ++i) {
      llvm::Type* fty = llvmTypeForTypeName(info->field_types[i]);
      llvm::Value* gep =
          builder_.CreateStructGEP(info->type, tmp, static_cast<unsigned>(i), "str.lit.z");
      builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
    }
    const auto& lit = static_cast<const ast::StringLiteralExpr&>(expr);
    llvm::Value* cstr = builder_.CreateGlobalString(lit.value, "str");
    builder_.CreateCall(init_it->second, {tmp, cstr});
    return tmp;
  }
  llvm::Value* v = emitExpr(expr);
  if (!v) {
    return nullptr;
  }
  if (v->getType()->isPointerTy()) {
    return v;
  }
  if (v->getType() == info->type) {
    llvm::Function* cur = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, "str.tmp");
    builder_.CreateStore(v, tmp);
    return tmp;
  }
  error(expr.loc, "expected String object");
  return nullptr;
}

llvm::Value* Codegen::emitStringByValue(const ast::Expr& expr) {
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error(expr.loc, "String type not available");
    return nullptr;
  }
  llvm::Value* ptr = emitStringObjectPtr(expr);
  if (!ptr) {
    return nullptr;
  }
  return builder_.CreateLoad(info->type, ptr, "str.val");
}

llvm::Value* Codegen::emitIntToStringObject(llvm::Value* i32_val) {
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error(lastTopLevelLoc(), "String type not available for i32→String");
    // [A007] no AST loc available
    return nullptr;
  }
  llvm::Function* to_str = requireExtern("l_int_to_str", lastTopLevelLoc());
  auto init_it = info->methods.find("init");
  if (!to_str || init_it == info->methods.end()) {
    error(lastTopLevelLoc(), "l_int_to_str / String.init required for interpolation");
    // [A007] no AST loc available
    return nullptr;
  }
  llvm::Value* cstr = builder_.CreateCall(to_str, {i32_val}, "int.str");
  llvm::Function* cur = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, "int.str.obj");
  for (size_t i = 0; i < info->field_types.size(); ++i) {
    llvm::Type* fty = llvmTypeForTypeName(info->field_types[i]);
    llvm::Value* gep =
        builder_.CreateStructGEP(info->type, tmp, static_cast<unsigned>(i), "int.str.z");
    builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
  }
  builder_.CreateCall(init_it->second, {tmp, cstr});
  return tmp;
}

llvm::Value* Codegen::emitAsStringObjectPtr(const ast::Expr& expr) {
  // Nested interpolation / String+ already yields an owning heap String*.
  if (expr.kind() == ast::ExprKind::Binary &&
      isStringConcatBinary(static_cast<const ast::BinaryExpr&>(expr))) {
    return emitExpr(expr);
  }
  if (isStringOperand(expr)) {
    return emitStringObjectPtr(expr);
  }
  std::string ty = exprTypeOf(expr);
  if (ty.empty() && expr.kind() == ast::ExprKind::IntLiteral) {
    ty = "i32";
  }
  if (ty == "i32" || expr.kind() == ast::ExprKind::IntLiteral) {
    llvm::Value* v = toI32(emitExpr(expr), expr.loc);
    if (!v) {
      return nullptr;
    }
    return emitIntToStringObject(v);
  }
  if (ty == "i64" || ty == "f64" || ty == "bool") {
    llvm::Value* v = emitExpr(expr);
    if (!v) {
      return nullptr;
    }
    const char* extern_name = ty == "i64" ? "l_i64_to_str" : (ty == "f64" ? "l_f64_to_str" : "l_bool_to_str");
    llvm::Function* to_str = requireExtern(extern_name, expr.loc);
    ensureTypeAvailable("String");
    const ClassInfo* info = findClass("String");
    if (!to_str || !info) {
      error(expr.loc, std::string(extern_name) + " / String required for concatenation");
      return nullptr;
    }
    auto init_it = info->methods.find("init");
    if (init_it == info->methods.end()) {
      error(expr.loc, "String.init required for concatenation");
      return nullptr;
    }
    if (ty == "bool") {
      v = builder_.CreateZExt(toI1(v), i32Ty(), "bool.i32");
    }
    llvm::Value* cstr = builder_.CreateCall(to_str, {v}, "num.str");
    llvm::Function* cur = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, "num.str.obj");
    for (size_t i = 0; i < info->field_types.size(); ++i) {
      llvm::Type* fty = llvmTypeForTypeName(info->field_types[i]);
      llvm::Value* gep =
          builder_.CreateStructGEP(info->type, tmp, static_cast<unsigned>(i), "num.str.z");
      builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
    }
    builder_.CreateCall(init_it->second, {tmp, cstr});
    return tmp;
  }
  error(expr.loc, "cannot convert expression to String for concatenation");
  return nullptr;
}

llvm::Value* Codegen::emitStringAdd(const ast::BinaryExpr& bin) {
  // a + b  ⇒  a.concat(b)  for String / string-literal / i32 (auto from) operands.
  // concat(String other) is pass-by-ref → pass String* for both this and other.
  // Result is heap-allocated so `t := a + b` can auto-free the owned String*.  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error(bin.loc, "String type not available for operator +");
    return nullptr;
  }
  auto mit = info->methods.find("concat");
  if (mit == info->methods.end()) {
    error(bin.loc, "String.concat not found for operator + lowering");
    return nullptr;
  }
  llvm::Value* lhs = emitAsStringObjectPtr(*bin.left);
  llvm::Value* rhs = emitAsStringObjectPtr(*bin.right);
  if (!lhs || !rhs) {
    return nullptr;
  }
  llvm::Value* by_val = builder_.CreateCall(mit->second, {lhs, rhs}, "str.add");
  ensureDataLayout();
  llvm::Function* alloc = requireExtern("l_alloc", bin.loc);
  if (!alloc) {
    return nullptr;
  }
  const uint64_t bytes = module_->getDataLayout().getTypeAllocSize(info->type);
  llvm::Value* size = llvm::ConstantInt::get(i64Ty(), bytes);
  llvm::Value* raw = builder_.CreateCall(alloc, {size}, "str.add.heap");
  builder_.CreateStore(by_val, raw);
  return raw;
}

llvm::Value* Codegen::emitArrayEachInline(const std::string& arr_name, const ClassInfo& info,
                                          const ast::LambdaExpr& lambda) {
  // Desugar arr.each { |x| body } → index while + direct lambda/closure call.
  auto len_fit = info.field_index.find("length_");
  auto get_it = info.methods.find("get");
  if (len_fit == info.field_index.end() || get_it == info.methods.end()) {
    error(lambda.loc, "Array missing length_ / get for each inlining");
    return nullptr;
  }
  if (info.type_args.empty()) {
    error(lambda.loc, "Array.each inline requires a specialized Array<T>");
    return nullptr;
  }
  const std::string elem_ty = info.type_args[0];
  const std::string fun_ty = "fun(" + elem_ty + ")";

  pending_closure_ctx_ = nullptr;
  pending_closure_ctx_ty_ = nullptr;
  llvm::Value* fn_val = emitLambda(lambda, fun_ty);
  if (!fn_val) {
    return nullptr;
  }
  llvm::Function* lam_fn = llvm::dyn_cast<llvm::Function>(fn_val);
  if (!lam_fn) {
    error(lambda.loc, "each inline: lambda did not lower to a function");
    return nullptr;
  }
  llvm::Value* ctx = pending_closure_ctx_;
  pending_closure_ctx_ = nullptr;
  pending_closure_ctx_ty_ = nullptr;

  llvm::Function* cur = builder_.GetInsertBlock()->getParent();
  static unsigned each_seq = 0;
  const std::string idx_name = "__each_i_" + std::to_string(each_seq++);
  llvm::AllocaInst* idx_slot = createEntryAlloca(cur, i32Ty(), idx_name);
  builder_.CreateStore(llvm::ConstantInt::get(i32Ty(), 0), idx_slot);

  llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(globalContext(), "each.cond", cur);
  llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(globalContext(), "each.body", cur);
  llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(globalContext(), "each.end", cur);

  branchTo(cond_bb);
  builder_.SetInsertPoint(cond_bb);
  llvm::Value* arr_ptr = classObjectPtr(arr_name, lambda.loc);
  if (!arr_ptr) {
    return nullptr;
  }
  llvm::Value* len_gep =
      builder_.CreateStructGEP(info.type, arr_ptr, len_fit->second, "each.len.ptr");
  llvm::Value* len = builder_.CreateLoad(i32Ty(), len_gep, "each.len");
  llvm::Value* idx = builder_.CreateLoad(i32Ty(), idx_slot, "each.i");
  llvm::Value* cmp = builder_.CreateICmpSLT(idx, len, "each.cmp");
  builder_.CreateCondBr(cmp, body_bb, end_bb);

  builder_.SetInsertPoint(body_bb);
  arr_ptr = classObjectPtr(arr_name, lambda.loc);
  idx = builder_.CreateLoad(i32Ty(), idx_slot, "each.i.body");
  llvm::Value* elem = builder_.CreateCall(get_it->second, {arr_ptr, idx}, "each.elem");

  std::vector<llvm::Value*> call_args;
  if (ctx) {
    call_args.push_back(ctx);
  }
  call_args.push_back(elem);
  if (lam_fn->arg_size() != call_args.size()) {
    error(lambda.loc, "each inline: lambda arity mismatch");    return nullptr;
  }
  builder_.CreateCall(lam_fn, call_args);

  if (builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    llvm::Value* cur_i = builder_.CreateLoad(i32Ty(), idx_slot, "each.i.inc");
    llvm::Value* next =
        builder_.CreateAdd(cur_i, llvm::ConstantInt::get(i32Ty(), 1), "each.i.next");
    builder_.CreateStore(next, idx_slot);
    branchTo(cond_bb);
  }

  builder_.SetInsertPoint(end_bb);
  return llvm::ConstantInt::get(i32Ty(), 0);
}

llvm::Value* Codegen::emitDelayInline(const ast::Expr& ms_expr, const ast::LambdaExpr& lambda,
                                      const ast::SourceLocation& loc) {
  // Lower `delay(ms) { body }` to `spawn { sleep(ms); body }` at the call site so
  // captures stay in the caller's frame (and trailing capturing blocks work).
  llvm::Value* ms_v = emitExpr(ms_expr);
  if (!ms_v) {
    return nullptr;
  }
  ms_v = toI32(ms_v, loc);
  if (!ms_v) {
    return nullptr;
  }
  llvm::Function* cur = builder_.GetInsertBlock()->getParent();
  const std::string ms_name = "__delay_ms_" + std::to_string(++delay_seq_);
  llvm::AllocaInst* ms_slot = createEntryAlloca(cur, i32Ty(), ms_name);
  builder_.CreateStore(ms_v, ms_slot);
  named_values_[ms_name] = ms_slot;
  local_types_[ms_name] = "i32";

  auto ms_id = std::make_unique<ast::IdentifierExpr>();
  ms_id->loc = loc;
  ms_id->name = ms_name;
  ast::CallArg sleep_arg;
  sleep_arg.loc = loc;
  sleep_arg.value = std::move(ms_id);
  auto sleep_call = std::make_unique<ast::CallExpr>();
  sleep_call->loc = loc;
  sleep_call->callee = "sleep";
  sleep_call->args.push_back(std::move(sleep_arg));
  auto sleep_stmt = std::make_unique<ast::ExprStmt>();
  sleep_stmt->loc = loc;
  sleep_stmt->expr = std::move(sleep_call);

  std::vector<ast::StmtPtr> body;
  body.push_back(std::move(sleep_stmt));
  // Steal statements from the call-site lambda (consumed once during codegen).
  auto& mut_lambda = const_cast<ast::LambdaExpr&>(lambda);
  for (auto& s : mut_lambda.body) {
    if (s) {
      body.push_back(std::move(s));
    }
  }
  mut_lambda.body.clear();

  llvm::Value* handle = emitSpawnBody(body, loc);
  if (!handle) {
    return nullptr;
  }
  return llvm::ConstantInt::get(i32Ty(), 0);
}

llvm::Value* Codegen::emitArenaBuiltin(const ast::CallExpr& call) {
  if (call.args.size() != 1 || !call.args[0].value) {
    error(call.loc, "arena(size) expects one size argument");
    return nullptr;
  }
  llvm::Function* fn = requireExtern("l_arena_new", call.loc);
  if (!fn) {
    return nullptr;
  }
  llvm::Value* size = emitExpr(*call.args[0].value);
  if (!size) {
    return nullptr;
  }
  if (!size->getType()->isIntegerTy(64)) {
    size = builder_.CreateIntCast(size, i64Ty(), /*isSigned=*/true, "arena.size");
  }
  return builder_.CreateCall(fn, {size}, "arena.new");
}

llvm::Value* Codegen::emitArenaMethod(const std::string& arena_name,
                                      const ast::MethodCallExpr& call) {
  auto vit = named_values_.find(arena_name);
  if (vit == named_values_.end()) {
    error(call.loc, "unknown arena: " + arena_name);
    return nullptr;
  }
  llvm::Value* arena_ptr = builder_.CreateLoad(ptrTy(), vit->second, arena_name + ".ptr");

  if (call.method == "alloc") {
    if (call.args.size() != 1 || !call.args[0].value) {
      error(call.loc, "arena.alloc(size) expects one size argument");
      return nullptr;
    }
    llvm::Function* alloc_fn = requireExtern("l_arena_alloc", call.loc);
    if (!alloc_fn) {
      return nullptr;
    }
    llvm::Value* size = emitExpr(*call.args[0].value);
    if (!size) {
      return nullptr;
    }
    if (!size->getType()->isIntegerTy(64)) {
      size = builder_.CreateIntCast(size, i64Ty(), /*isSigned=*/true, "arena.alloc.size");
    }
    return builder_.CreateCall(alloc_fn, {arena_ptr, size}, "arena.alloc");
  }

  if (call.method == "new_string") {
    if (call.args.size() != 1 || !call.args[0].value) {
      error(call.loc, "arena.new_string expects one string argument");
      return nullptr;
    }
    ensureTypeAvailable("String");
    const ClassInfo* info = findClass("String");
    if (!info) {
      error(call.loc, "String type not available for arena.new_string");
      return nullptr;
    }
    llvm::Function* alloc_fn = requireExtern("l_arena_alloc", call.loc);
    if (!alloc_fn) {
      return nullptr;
    }
    ensureDataLayout();
    const uint64_t obj_bytes = module_->getDataLayout().getTypeAllocSize(info->type);
    llvm::Value* obj_size = llvm::ConstantInt::get(i64Ty(), obj_bytes);
    llvm::Value* obj = builder_.CreateCall(alloc_fn, {arena_ptr, obj_size}, "arena.str.obj");

    const ast::Expr& arg = *call.args[0].value;
    if (arg.kind() != ast::ExprKind::StringLiteral) {
      error(call.loc, "arena.new_string currently requires a string literal");
      return nullptr;
    }
    const auto& lit = static_cast<const ast::StringLiteralExpr&>(arg);
    // Copy literal bytes into the arena so the String is fully bump-owned.
    llvm::Value* lit_g = builder_.CreateGlobalString(lit.value, "arena.str.lit");
    const uint64_t n = lit.value.size() + 1;
    llvm::Value* n_val = llvm::ConstantInt::get(i64Ty(), n);
    llvm::Value* dst = builder_.CreateCall(alloc_fn, {arena_ptr, n_val}, "arena.str.bytes");
    builder_.CreateMemCpy(dst, llvm::MaybeAlign(1), lit_g, llvm::MaybeAlign(1),
                          llvm::ConstantInt::get(i64Ty(), n));

    auto init_it = info->methods.find("init");
    if (init_it == info->methods.end()) {
      error(call.loc, "String.init not found");
      return nullptr;
    }
    for (size_t i = 0; i < info->field_types.size(); ++i) {
      llvm::Type* fty = llvmTypeForTypeName(info->field_types[i]);
      llvm::Value* gep =
          builder_.CreateStructGEP(info->type, obj, static_cast<unsigned>(i), "arena.str.z");
      builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
    }
    builder_.CreateCall(init_it->second, {obj, dst});
    return obj;
  }

  if (call.method == "new") {
    if (call.args.size() != 1 || !call.args[0].value ||
        call.args[0].value->kind() != ast::ExprKind::New) {
      error(call.loc, "arena.new expects ClassName(...)");
      return nullptr;
    }
    const auto& neu = static_cast<const ast::NewExpr&>(*call.args[0].value);
    if (!ensureTypeAvailable(neu.class_name)) {
      error(neu.loc, "unknown class in arena.new: " + neu.class_name);
      return nullptr;
    }
    const ClassInfo* info = findClass(neu.class_name);
    if (!info) {
      error(neu.loc, "unknown class in arena.new: " + neu.class_name);
      return nullptr;
    }
    llvm::Function* alloc_fn = requireExtern("l_arena_alloc", call.loc);
    if (!alloc_fn) {
      return nullptr;
    }
    ensureDataLayout();
    const uint64_t bytes = module_->getDataLayout().getTypeAllocSize(info->type);
    llvm::Value* size = llvm::ConstantInt::get(i64Ty(), bytes);
    llvm::Value* raw = builder_.CreateCall(alloc_fn, {arena_ptr, size}, "arena.obj");
    if (!initClassAtPtr(raw, *info, neu)) {
      return nullptr;
    }
    return raw;
  }

  error(call.loc, "unknown arena method: " + call.method);
  return nullptr;
}

// `obj?.m(args)` — safe call.
//
// Semantics: the receiver must be an `Optional<T>` (that is the only thing that
// can be "absent" in L; there are no implicit nulls). The result is always an
// `Optional<R>`:
//   - empty receiver  -> empty Optional<R>, `m` is never entered
//   - present receiver -> unwrap, call `m`, wrap the value in Optional<R>
// Chained `a?.b()?.c()` therefore short-circuits the whole chain.
llvm::Value* Codegen::emitSafeMethodCall(const ast::MethodCallExpr& call) {
  std::string recv_ty = exprTypeOf(*call.object);
  if (recv_ty.empty()) {
    recv_ty = inferTypeFromExpr(*call.object);
  }
  if (recv_ty.empty()) {
    error(call.loc, "cannot determine the receiver type of '?.' on '" + call.method + "'");
    return nullptr;
  }
  if (recv_ty.rfind("Optional<", 0) != 0) {
    // Not an Optional but a concrete value: `?.` degrades to a plain call, so a
    // chain can mix `?.` and direct receivers without rewriting the whole path.
    return emitMethodCall(call, call.object.get());
  }
  const std::string payload = recv_ty.substr(9, recv_ty.size() - 10);
  if (!ensureTypeAvailable(payload)) {
    error(call.loc, "cannot resolve Optional payload type '" + payload + "'");
    return nullptr;
  }
  // Result type: Optional<R> where R is the method's declared return type.
  std::string ret_ty_name = methodReturnTypeOn(payload, call.method);
  if (ret_ty_name.empty()) {
    error(call.loc, "unknown method '" + call.method + "' on '" + payload + "'");
    return nullptr;
  }
  // Result type: `Optional<R>` for a plain R. When the method already returns an
  // Optional it is FLATTENED — `a?.get_val()` is `Optional<Variant>`, not
  // `Optional<Optional<Variant>>`, so `??` selects the payload directly.
  const bool flatten = ret_ty_name.rfind("Optional<", 0) == 0;
  const std::string out_ty = flatten ? ret_ty_name : "Optional<" + ret_ty_name + ">";
  if (!ensureTypeAvailable(out_ty)) {
    error(call.loc, "cannot specialize " + out_ty);
    return nullptr;
  }
  const ClassInfo* opt_in = findClass(recv_ty);
  const ClassInfo* opt_out = findClass(out_ty);
  if (!opt_in || !opt_out) {
    return nullptr;
  }
  auto has_it = opt_in->field_index.find("has_value_");
  auto val_it = opt_in->field_index.find("value_");
  auto ohas_it = opt_out->field_index.find("has_value_");
  auto oval_it = opt_out->field_index.find("value_");
  if (has_it == opt_in->field_index.end() || val_it == opt_in->field_index.end() ||
      ohas_it == opt_out->field_index.end() || oval_it == opt_out->field_index.end()) {
    error(call.loc, "Optional layout missing has_value_/value_");
    return nullptr;
  }

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* out = createEntryAlloca(fn, opt_out->type, "safe.opt");
  for (size_t i = 0; i < opt_out->field_types.size(); ++i) {
    llvm::Type* fty = opt_out->type->getElementType(static_cast<unsigned>(i));
    llvm::Value* gep =
        builder_.CreateStructGEP(opt_out->type, out, static_cast<unsigned>(i), "safe.z");
    builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
  }

  // Evaluate the receiver exactly once.
  llvm::Value* recv = emitExpr(*call.object);
  if (!recv) {
    return nullptr;
  }
  if (recv->getType() == opt_in->type->getPointerTo()) {
    recv = builder_.CreateLoad(opt_in->type, recv, "safe.recv");
  }
  llvm::Value* recv_has =
      builder_.CreateExtractValue(recv, static_cast<unsigned>(has_it->second), "safe.has");

  llvm::BasicBlock* some_bb = llvm::BasicBlock::Create(globalContext(), "safe.some", fn);
  llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(globalContext(), "safe.merge", fn);
  builder_.CreateCondBr(recv_has, some_bb, merge_bb);

  builder_.SetInsertPoint(some_bb);
  llvm::Value* inner =
      builder_.CreateExtractValue(recv, static_cast<unsigned>(val_it->second), "safe.inner");
  // Receiver payload may be a class (pointer handle) or a plain value.
  llvm::Value* result = nullptr;
  {
    // Rebuild the call with the unwrapped payload bound to a hidden local so the
    // normal method-call path (delegates, statics, property fallback) still applies.
    const std::string hidden = "__safe_recv";
    llvm::AllocaInst* slot = createEntryAlloca(fn, inner->getType(), hidden);
    builder_.CreateStore(inner, slot);
    const auto saved_local = local_types_.find(hidden);
    const std::string saved_local_ty = (saved_local != local_types_.end()) ? saved_local->second : "";
    auto saved_nv = named_values_.find(hidden);
    llvm::AllocaInst* saved_nv_ptr = (saved_nv != named_values_.end())
                                        ? llvm::dyn_cast<llvm::AllocaInst>(saved_nv->second)
                                        : nullptr;

    local_types_[hidden] = payload;
    named_values_[hidden] = slot;

    ast::IdentifierExpr ident;
    ident.loc = call.loc;
    ident.name = hidden;
    result = emitMethodCall(call, &ident);

    if (saved_local == local_types_.end()) {
      local_types_.erase(hidden);
    } else {
      local_types_[hidden] = saved_local_ty;
    }
    if (saved_nv_ptr) {
      named_values_[hidden] = saved_nv_ptr;
    } else {
      named_values_.erase(hidden);
    }
  }
  if (!result) {
    return nullptr;
  }
  if (flatten) {
    // The method already returned the flattened `Optional<R>`: present receiver
    // just forwards it (an empty one is the "none" answer too).
    builder_.CreateStore(result, out);
    builder_.CreateBr(merge_bb);
    builder_.SetInsertPoint(merge_bb);
    return out;
  }
  // Normalize to the output payload representation.
  llvm::Value* out_val =
      builder_.CreateStructGEP(opt_out->type, out, static_cast<unsigned>(oval_it->second),
                               "safe.out.val");
  llvm::Type* want = opt_out->type->getElementType(static_cast<unsigned>(oval_it->second));
  if (result->getType() != want) {
    if (result->getType()->isPointerTy() && want->isStructTy()) {
      result = builder_.CreateLoad(want, result, "safe.load");
    } else if (!result->getType()->isPointerTy() && want->isPointerTy()) {
      // Value returned where a handle is expected (class payload): box on stack.
      llvm::AllocaInst* tmp = createEntryAlloca(fn, result->getType(), "safe.box");
      builder_.CreateStore(result, tmp);
      result = tmp;
      want = tmp->getType();
    }
  }
  builder_.CreateStore(result, out_val);
  llvm::Value* out_has =
      builder_.CreateStructGEP(opt_out->type, out, static_cast<unsigned>(ohas_it->second),
                               "safe.out.has");
  builder_.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt1Ty(globalContext()), 1),
                       out_has);
  builder_.CreateBr(merge_bb);

  builder_.SetInsertPoint(merge_bb);
  return out;
}

// `obj?.field` — safe field / property access. Mirrors emitSafeMethodCall:
// evaluate the receiver once, branch on `has_value_`, and produce
// `Optional<FieldType>` (flattened when the field is already an Optional).
llvm::Value* Codegen::emitSafeMemberAccess(const ast::MemberAccessExpr& access) {
  if (!access.object) {
    error(access.loc, "member access missing object");
    return nullptr;
  }
  std::string recv_ty = exprTypeOf(*access.object);
  if (recv_ty.empty()) {
    recv_ty = inferTypeFromExpr(*access.object);
  }
  if (recv_ty.empty()) {
    error(access.loc, "cannot determine the receiver type of '?.' on '" + access.member + "'");
    return nullptr;
  }
  if (recv_ty.rfind("Optional<", 0) != 0) {
    // Concrete receiver: `?.` degrades to a plain field access.
    return emitExpr(access);
  }
  const std::string payload = recv_ty.substr(9, recv_ty.size() - 10);
  if (!ensureTypeAvailable(payload) || !isClassType(payload)) {
    error(access.loc, "'?.' on '" + recv_ty + "' requires a class payload, got '" + payload + "'");
    return nullptr;
  }
  const ClassInfo* inner_info = findClass(payload);
  if (!inner_info) {
    error(access.loc, "unknown class '" + payload + "' in '?.'");
    return nullptr;
  }
  const PropResolve pr = resolveProperty(*inner_info, access.member, PropAccess::Read);
  std::string field_ty;
  if (pr.kind == PropResolveKind::Method) {
    for (const auto& m : inner_info->ast->methods) {
      if (m && m->name == pr.method_name) {
        field_ty = instantiateMemberType(m->return_type, *inner_info);
        break;
      }
    }
  } else if (pr.kind == PropResolveKind::Field) {
    auto fit = inner_info->field_index.find(pr.field_name);
    if (fit != inner_info->field_index.end()) {
      field_ty = inner_info->field_types[fit->second];
    }
  }
  if (field_ty.empty()) {
    error(access.loc, "unknown property '" + access.member + "' on '" + payload + "'");
    return nullptr;
  }
  // An Optional field stays flat: `opt?.maybe` is Optional<T>, never Optional<Optional<T>>.
  const bool flatten = field_ty.rfind("Optional<", 0) == 0;
  const std::string out_ty = flatten ? field_ty : "Optional<" + field_ty + ">";
  if (!ensureTypeAvailable(out_ty)) {
    error(access.loc, "cannot specialize " + out_ty);
    return nullptr;
  }
  const ClassInfo* opt_in = findClass(recv_ty);
  const ClassInfo* opt_out = findClass(out_ty);
  if (!opt_in || !opt_out) {
    return nullptr;
  }
  auto has_it = opt_in->field_index.find("has_value_");
  auto val_it = opt_in->field_index.find("value_");
  auto ohas_it = opt_out->field_index.find("has_value_");
  auto oval_it = opt_out->field_index.find("value_");
  if (has_it == opt_in->field_index.end() || val_it == opt_in->field_index.end() ||
      ohas_it == opt_out->field_index.end() || oval_it == opt_out->field_index.end()) {
    error(access.loc, "Optional layout missing has_value_/value_");
    return nullptr;
  }

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* out = createEntryAlloca(fn, opt_out->type, "safe.field.opt");
  for (size_t i = 0; i < opt_out->field_types.size(); ++i) {
    llvm::Type* fty = opt_out->type->getElementType(static_cast<unsigned>(i));
    llvm::Value* gep =
        builder_.CreateStructGEP(opt_out->type, out, static_cast<unsigned>(i), "safe.field.z");
    builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
  }

  llvm::Value* recv = emitExpr(*access.object);
  if (!recv) {
    return nullptr;
  }
  if (recv->getType() == opt_in->type->getPointerTo()) {
    recv = builder_.CreateLoad(opt_in->type, recv, "safe.field.recv");
  }
  llvm::Value* recv_has = builder_.CreateExtractValue(
      recv, static_cast<unsigned>(has_it->second), "safe.field.has");

  llvm::BasicBlock* some_bb = llvm::BasicBlock::Create(globalContext(), "safe.field.some", fn);
  llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(globalContext(), "safe.field.merge", fn);
  builder_.CreateCondBr(recv_has, some_bb, merge_bb);

  builder_.SetInsertPoint(some_bb);
  llvm::Value* inner = builder_.CreateExtractValue(
      recv, static_cast<unsigned>(val_it->second), "safe.field.inner");

  // Bind the unwrapped payload to a hidden local and re-run the normal property
  // path (getter / pub-field fallback) against it.
  llvm::Value* result = nullptr;
  {
    const std::string hidden = "__safe_field_recv";
    llvm::AllocaInst* slot = createEntryAlloca(fn, inner->getType(), hidden);
    builder_.CreateStore(inner, slot);
    const auto saved_local = local_types_.find(hidden);
    const std::string saved_local_ty =
        (saved_local != local_types_.end()) ? saved_local->second : "";
    auto saved_nv = named_values_.find(hidden);
    llvm::AllocaInst* saved_nv_ptr = (saved_nv != named_values_.end())
                                        ? llvm::dyn_cast<llvm::AllocaInst>(saved_nv->second)
                                        : nullptr;

    local_types_[hidden] = payload;
    named_values_[hidden] = slot;

    ast::IdentifierExpr ident;
    ident.loc = access.loc;
    ident.name = hidden;
    ast::MemberAccessExpr plain;
    plain.loc = access.loc;
    plain.member = access.member;
    plain.object = std::make_unique<ast::IdentifierExpr>(ident);
    plain.is_safe = false;
    result = emitExpr(plain);

    if (saved_local == local_types_.end()) {
      local_types_.erase(hidden);
    } else {
      local_types_[hidden] = saved_local_ty;
    }
    if (saved_nv_ptr) {
      named_values_[hidden] = saved_nv_ptr;
    } else {
      named_values_.erase(hidden);
    }
  }
  if (!result) {
    return nullptr;
  }
  if (flatten) {
    builder_.CreateStore(result, out);
    builder_.CreateBr(merge_bb);
    builder_.SetInsertPoint(merge_bb);
    return out;
  }

  // Normalize the payload representation (class handles vs. plain values).
  llvm::Value* out_val = builder_.CreateStructGEP(
      opt_out->type, out, static_cast<unsigned>(oval_it->second), "safe.field.out.val");
  llvm::Type* want = opt_out->type->getElementType(static_cast<unsigned>(oval_it->second));
  if (result->getType() != want) {
    if (result->getType()->isPointerTy() && want->isStructTy()) {
      result = builder_.CreateLoad(want, result, "safe.field.load");
    } else if (!result->getType()->isPointerTy() && want->isPointerTy()) {
      llvm::AllocaInst* tmp = createEntryAlloca(fn, result->getType(), "safe.field.box");
      builder_.CreateStore(result, tmp);
      result = tmp;
    }
  }
  builder_.CreateStore(result, out_val);
  llvm::Value* out_has = builder_.CreateStructGEP(
      opt_out->type, out, static_cast<unsigned>(ohas_it->second), "safe.field.out.has");
  builder_.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt1Ty(globalContext()), 1),
                       out_has);
  builder_.CreateBr(merge_bb);

  builder_.SetInsertPoint(merge_bb);
  return out;
}

llvm::Value* Codegen::emitClassMethodCall(const ast::MethodCallExpr& call,
                                         const ClassInfo& cinfo, llvm::Value* self_ptr,
                                         const std::string& class_name) {
  llvm::Function* callee_fn = nullptr;
  const ClassInfo* callee_info = &cinfo;
  llvm::Value* this_ptr = self_ptr;

  auto mit = cinfo.methods.find(call.method);
  if (mit != cinfo.methods.end()) {
    callee_fn = mit->second;
  } else if (cinfo.ast) {
    struct DelegateHit {
      std::string type_name;
      unsigned field_idx = 0;
      llvm::Function* fn = nullptr;
      const ClassInfo* emb = nullptr;
    };
    std::vector<DelegateHit> hits;
    for (const auto& field : cinfo.ast->fields) {
      if (!field || !field->is_delegate) {
        continue;
      }
      if (!ensureTypeAvailable(field->type_name) || !isClassType(field->type_name)) {
        continue;
      }
      const ClassInfo* emb = findClass(field->type_name);
      if (!emb) {
        continue;
      }
      auto emit = emb->methods.find(call.method);
      if (emit == emb->methods.end()) {
        continue;
      }
      auto fit = cinfo.field_index.find(physicalFieldName(field->name));
      if (fit == cinfo.field_index.end()) {
        error(call.loc, "delegate field layout missing for '" + field->type_name + "'");
        return nullptr;
      }
      hits.push_back(DelegateHit{field->type_name, fit->second, emit->second, emb});
    }
    if (hits.size() >= 2) {
      error(call.loc, "Ambiguous method '" + call.method + "': defined in both '" +
                          hits[0].type_name + "' and '" + hits[1].type_name + "'.");
      return nullptr;
    }
    if (hits.size() == 1) {
      this_ptr = builder_.CreateStructGEP(cinfo.type, self_ptr, hits[0].field_idx,
                                          "delegate." + hits[0].type_name);
      callee_fn = hits[0].fn;
      callee_info = hits[0].emb;
    }
  }
  if (!callee_fn) {
    return nullptr;  // not a method on this class / its delegates
  }

  std::vector<const ast::Expr*> ordered;
  const ast::FuncDeclStmt* method_ast = nullptr;
  if (callee_info->ast) {
    for (const auto& m : callee_info->ast->methods) {
      if (m && m->name == call.method) {
        method_ast = m.get();
        break;
      }
    }
  }
  if (method_ast) {
    if (!resolveCallArgs(*method_ast, call.args, ordered)) {
      return nullptr;
    }
  } else {
    for (const ast::CallArg& a : call.args) {
      if (!a.name.empty()) {
        error(call.loc, "keyword arguments require a known method signature");
        return nullptr;
      }
      if (!a.value) {
        error(call.loc, "null argument in method call");
        return nullptr;
      }
      ordered.push_back(a.value.get());
    }
  }

  const bool is_static = method_ast && method_ast->is_static;
  std::vector<llvm::Value*> call_args;
  unsigned ai = 0;
  if (!is_static) {
    call_args.push_back(this_ptr);
    ai = 1;
  }
  for (const ast::Expr* arg : ordered) {
    llvm::Type* expected = callee_fn->getFunctionType()->getParamType(ai);
    std::string expected_name;
    const size_t param_i = is_static ? static_cast<size_t>(ai) : static_cast<size_t>(ai - 1);
    if (method_ast && param_i < method_ast->params.size()) {
      expected_name =
          instantiateMemberType(method_ast->params[param_i].type_name, *callee_info);
    }
    ++ai;
    llvm::Value* v = emitValueForParam(*arg, expected, expected_name);
    if (!v) {
      return nullptr;
    }
    call_args.push_back(v);
  }
  if (callee_fn->arg_size() != call_args.size()) {
    error(call.loc, "argument count mismatch calling " + class_name + "." + call.method);
    return nullptr;
  }
  if (callee_fn->getReturnType()->isVoidTy()) {
    builder_.CreateCall(callee_fn, call_args);
    return llvm::ConstantInt::get(i32Ty(), 0);
  }
  return builder_.CreateCall(callee_fn, call_args, is_static ? "static.call" : "mcall");
}

// [dsl] `app { ... }` / `app(type: App) { ... }`.
// Builds the root object (default `CPropertys`, or the `type:` class), runs the
// bare-brace block inline with `this` bound to it, then makes the ordinary call
// so the callee can post-process the finished tree.
bool Codegen::initConfigSections(const ClassInfo& info, llvm::Value* base, int depth) {
  // Self-referential config classes would recurse forever; a few levels is all
  // a hand-written config tree can use (App → Server → Database → ...).
  if (depth > 8 || !base || !info.type || !info.type->isSized()) {
    return true;
  }
  for (size_t i = 0; i < info.field_types.size(); ++i) {
    const std::string fty_name = resolveTypeName(info.field_types[i]);
    if (fty_name.empty() || !isClassType(fty_name)) {
      continue;
    }
    const ClassInfo* finfo = findClass(fty_name);
    if (!finfo || !finfo->type) {
      continue;
    }
    llvm::Value* slot =
        builder_.CreateStructGEP(info.type, base, static_cast<unsigned>(i), "config.slot");
    // The parent was just zero-filled; hand the child a zeroed slot and let the
    // user's `init()` (if any, no extra args) run: `this` is already live.
    builder_.CreateStore(llvm::Constant::getNullValue(finfo->type), slot);
    auto init_it = finfo->methods.find("init");
    if (init_it != finfo->methods.end() && init_it->second &&
        init_it->second->arg_size() == 1) {
      builder_.CreateCall(init_it->second, {slot});
    }
    if (!initConfigSections(*finfo, slot, depth + 1)) {
      return false;
    }
  }
  return true;
}

llvm::Value* Codegen::emitConfigBlockCall(const ast::CallExpr& call,
                                         const ast::LambdaExpr& block) {
  std::string root_ty = call.config_type;
  if (root_ty.empty()) {
    root_ty = "CPropertys";
  }
  if (!ensureTypeAvailable(root_ty) || !isClassType(root_ty)) {
    error(call.loc, "unknown config type '" + root_ty + "'");
    return nullptr;
  }
  const ClassInfo* rinfo = findClass(root_ty);
  if (!rinfo) {
    return nullptr;
  }
  llvm::Function* cur = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* root =
      createEntryAlloca(cur, rinfo->type, root_ty + ".config");
  if (!initClassAtPtr(root, *rinfo, ast::NewExpr{})) {
    return nullptr;
  }
  // Class-typed fields are embedded by value: after zero-filling the root the
  // nested `Server` / `Database` are still uninitialized. Build the whole tree
  // top-down here (once, before the block body runs) so nothing clobbers it.
  if (!initConfigSections(*rinfo, root, 0)) {
    return nullptr;
  }
  if (!emitConfigBlockInline(block, root, root_ty)) {
    return nullptr;
  }

  // [dsl] `cfg := app(type: App) { ... }` — the root built above *is* the
  // value of `cfg`; hand it to the declaration so no helper call is emitted.
  if (!call.config_bind_target.empty()) {
    return root;
  }

  // Ordinary call, but the block argument is the already-built root.
  auto fit = concrete_funcs_.find(call.callee);
  if (fit == concrete_funcs_.end() && !current_module_.empty()) {
    fit = concrete_funcs_.find(current_module_ + "::" + call.callee);
  }
  if (fit == concrete_funcs_.end() || !fit->second) {
    error(call.loc, "unknown function: " + call.callee);
    return nullptr;
  }
  const ast::FuncDeclStmt* fdecl = fit->second;
  const std::string ir_name = resolveFuncIrName(call.callee);
  llvm::Function* callee = ir_name.empty() ? nullptr : module_->getFunction(ir_name);
  if (!callee) {
    error(call.loc, "missing function IR: " + call.callee);
    return nullptr;
  }
  std::vector<const ast::Expr*> ordered;
  if (!resolveCallArgs(*fdecl, call.args, ordered)) {
    return nullptr;
  }
  if (callee->arg_size() != ordered.size()) {
    error(call.loc, "argument count mismatch calling " + call.callee);
    return nullptr;
  }
  std::vector<llvm::Value*> call_args;
  for (size_t i = 0; i < ordered.size(); ++i) {
    llvm::Type* expected = callee->getFunctionType()->getParamType(static_cast<unsigned>(i));
    if (ordered[i] && ordered[i]->kind() == ast::ExprKind::Lambda &&
        static_cast<const ast::LambdaExpr&>(*ordered[i]).is_config_dsl) {
      call_args.push_back(builder_.CreateBitCast(root, expected, "config.root"));
      continue;
    }
    std::string expected_name;
    if (i < fdecl->params.size()) {
      expected_name = resolveTypeName(fdecl->params[i].type_name);
    }
    llvm::Value* v = emitValueForParam(*ordered[i], expected, expected_name);
    if (!v) {
      return nullptr;
    }
    call_args.push_back(v);
  }
  if (callee->getReturnType()->isVoidTy()) {
    builder_.CreateCall(callee, call_args);
    return llvm::ConstantInt::get(i32Ty(), 0);
  }
  return builder_.CreateCall(callee, call_args, "config.call");
}

llvm::Value* Codegen::emitMethodCall(const ast::MethodCallExpr& call,
                                     const ast::Expr* object_override) {
  const ast::Expr* object = object_override ? object_override : call.object.get();
  // `obj?.m(...)` — safe call. If the receiver is an empty `Optional<T>`, the
  // whole chain yields an empty `Optional<R>` without ever touching the payload.
  if (call.is_safe && !object_override && call.object) {
    return emitSafeMethodCall(call);
  }
  if (!object) {
    error(call.loc, "method call missing object");
    return nullptr;
  }

  // MemberAccess / other receivers: e.g. `c.Engine.start()` (explicit delegate).
  if (object->kind() != ast::ExprKind::Identifier) {
    std::string recv_ty = exprTypeOf(*object);
    if (recv_ty.empty()) {
      recv_ty = inferTypeFromExpr(*object);
    }
    if (!recv_ty.empty()) {
      ensureTypeAvailable(recv_ty);
    }
    // Interface receiver produced by an expression, e.g.
    // `arr.get(i).on_start()` / `opt.unwrap().name()`. Dispatch through the
    // fat pointer's vtable (same layout as the identifier receiver path).
    if (isInterfaceType(recv_ty)) {
      const InterfaceInfo* iface = findInterface(recv_ty);
      if (!iface) {
        error(call.loc, "unknown interface: " + recv_ty);
        return nullptr;
      }
      auto mit = iface->method_index.find(call.method);
      if (mit == iface->method_index.end()) {
        error(call.loc, "unknown interface method " + recv_ty + "." + call.method);
        return nullptr;
      }
      const ast::FuncDeclStmt* sig = iface->methods.at(call.method);
      if (!sig) {
        error(call.loc, "interface method signature missing for " + call.method);
        return nullptr;
      }

      llvm::Value* fat = emitExpr(*object);
      if (!fat) {
        return nullptr;
      }
      if (fat->getType() != fatPtrTy()) {
        error(call.loc, "interface receiver did not produce a fat pointer");
        return nullptr;
      }
      llvm::Value* data = builder_.CreateExtractValue(fat, 0, "iface.data");
      llvm::Value* vtable = builder_.CreateExtractValue(fat, 1, "iface.vtable");

      llvm::ArrayType* arr_ty = vtableArrayTy(*iface);
      llvm::Value* slot_ptr = builder_.CreateInBoundsGEP(
          arr_ty, vtable,
          {llvm::ConstantInt::get(i32Ty(), 0), llvm::ConstantInt::get(i32Ty(), mit->second)},
          "chained.vtable.slot");
      llvm::Value* fn_ptr = builder_.CreateLoad(ptrTy(), slot_ptr, "chained.fnptr");

      std::vector<llvm::Type*> param_tys{ptrTy()};
      for (const ast::Param& p : sig->params) {
        param_tys.push_back(abiTypeForParam(p.type_name));
      }
      llvm::Type* ret_ty = typeFromName(sig->return_type);
      auto* fty = llvm::FunctionType::get(ret_ty, param_tys, false);

      std::vector<llvm::Value*> call_args{data};
      unsigned ai = 1;
      for (size_t i = 0; i < call.args.size(); ++i) {
        if (!call.args[i].value) {
          return nullptr;
        }
        if (!call.args[i].name.empty()) {
          error(call.loc, "keyword arguments not supported on interface dispatch yet");
          return nullptr;
        }
        llvm::Type* expected = ai < param_tys.size() ? param_tys[ai] : nullptr;
        llvm::Value* v = emitValueForParam(*call.args[i].value, expected,
                                           resolveTypeName(sig->params[i].type_name));
        if (!v) {
          return nullptr;
        }
        ++ai;
        call_args.push_back(v);
      }
      if (ret_ty->isVoidTy()) {
        builder_.CreateCall(fty, fn_ptr, call_args);
        return llvm::ConstantInt::get(i32Ty(), 0);
      }
      return builder_.CreateCall(fty, fn_ptr, call_args, "chained.call");
    }
    if (!isClassType(recv_ty)) {
      error(call.loc, "method call object must be a class or interface value");
      return nullptr;
    }
    const ClassInfo* info = findClass(recv_ty);
    if (!info) {
      error(call.loc, "unknown class in method call: " + recv_ty);
      return nullptr;
    }
    llvm::Function* callee_fn = nullptr;
    const ClassInfo* callee_info = info;
    auto own = info->methods.find(call.method);
    if (own != info->methods.end()) {
      callee_fn = own->second;
    } else if (info->ast) {
      // Nested promotion on the field's type (same ambiguity rule).
      struct DelegateHit {
        std::string type_name;
        unsigned field_idx = 0;
        llvm::Function* fn = nullptr;
        const ClassInfo* emb = nullptr;
      };
      std::vector<DelegateHit> hits;
      for (const auto& field : info->ast->fields) {
        if (!field || !field->is_delegate) {
          continue;
        }
        if (!ensureTypeAvailable(field->type_name) || !isClassType(field->type_name)) {
          continue;
        }
        const ClassInfo* emb = findClass(field->type_name);
        if (!emb) {
          continue;
        }
        auto emit = emb->methods.find(call.method);
        if (emit == emb->methods.end()) {
          continue;
        }
        const std::string phys = physicalFieldName(field->name);
        auto fit = info->field_index.find(phys);
        if (fit == info->field_index.end()) {
          continue;
        }
        hits.push_back(DelegateHit{field->type_name, fit->second, emit->second, emb});
      }
      if (hits.size() >= 2) {
        error(call.loc, "Ambiguous method '" + call.method + "': defined in both '" +
                            hits[0].type_name + "' and '" + hits[1].type_name + "'.");
        return nullptr;
      }
      if (hits.size() == 1) {
        llvm::Value* outer = classValuePtr(*object, recv_ty);
        if (!outer) {
          return nullptr;
        }
        llvm::Value* this_ptr = builder_.CreateStructGEP(info->type, outer, hits[0].field_idx,
                                                         "delegate." + hits[0].type_name);
        callee_fn = hits[0].fn;
        callee_info = hits[0].emb;
        // Emit call with delegate this immediately.
        std::vector<const ast::Expr*> ordered;
        const ast::FuncDeclStmt* method_ast = nullptr;
        if (callee_info->ast) {
          for (const auto& m : callee_info->ast->methods) {
            if (m && m->name == call.method) {
              method_ast = m.get();
              break;
            }
          }
        }
        if (method_ast) {
          if (!resolveCallArgs(*method_ast, call.args, ordered)) {
            return nullptr;
          }
        } else {
          for (const ast::CallArg& a : call.args) {
            if (!a.value) {
              return nullptr;
            }
            ordered.push_back(a.value.get());
          }
        }
        std::vector<llvm::Value*> call_args{this_ptr};
        unsigned ai = 1;
        for (const ast::Expr* arg : ordered) {
          if (ai >= callee_fn->arg_size()) {
            error(call.loc, "argument count mismatch calling " + recv_ty + "." + call.method);
            return nullptr;
          }
          llvm::Type* expected = callee_fn->getFunctionType()->getParamType(ai);
          std::string expected_name;
          if (method_ast && ai - 1 < method_ast->params.size()) {
            expected_name = method_ast->params[ai - 1].type_name;
          }
          if (expected_name.empty()) {
            // No AST available (e.g. delegate promoted from an already-registered
            // class): fall back to the argument's own inferred type so class
            // formals still bind by reference.
            expected_name = exprTypeOf(*arg);
            if (expected_name.empty()) {
              expected_name = inferTypeFromExpr(*arg);
            }
          }
          ++ai;
          llvm::Value* v = emitValueForParam(*arg, expected, expected_name);
          if (!v) {
            return nullptr;
          }
          call_args.push_back(v);
        }
        if (callee_fn->arg_size() != call_args.size()) {
          error(call.loc, "argument count mismatch calling " + recv_ty + "." + call.method);
          return nullptr;
        }
        if (callee_fn->getReturnType()->isVoidTy()) {
          builder_.CreateCall(callee_fn, call_args);
          return llvm::ConstantInt::get(i32Ty(), 0);
        }
        return builder_.CreateCall(callee_fn, call_args, "mcall");
      }
    }
    if (!callee_fn) {
      error(call.loc, "unknown method " + recv_ty + "." + call.method);
      return nullptr;
    }
    std::vector<const ast::Expr*> ordered;
    const ast::FuncDeclStmt* method_ast = nullptr;
    if (callee_info->ast) {
      for (const auto& m : callee_info->ast->methods) {
        if (m && m->name == call.method) {
          method_ast = m.get();
          break;
        }
      }
    }
    if (method_ast) {
      if (!resolveCallArgs(*method_ast, call.args, ordered)) {
        return nullptr;
      }
    } else {
      for (const ast::CallArg& a : call.args) {
        if (!a.value) {
          return nullptr;
        }
        ordered.push_back(a.value.get());
      }
    }
    const bool is_static = method_ast && method_ast->is_static;
    std::vector<llvm::Value*> call_args;
    unsigned ai = 0;
    if (!is_static) {
      llvm::Value* this_ptr = classValuePtr(*object, recv_ty);
      if (!this_ptr) {
        return nullptr;
      }
      call_args.push_back(this_ptr);
      ai = 1;
    }
    for (const ast::Expr* arg : ordered) {
      llvm::Type* expected = callee_fn->getFunctionType()->getParamType(ai);
      std::string expected_name;
      const size_t param_i = is_static ? static_cast<size_t>(ai) : static_cast<size_t>(ai - 1);
      if (method_ast && param_i < method_ast->params.size()) {
        expected_name =
            instantiateMemberType(method_ast->params[param_i].type_name, *callee_info);
      }
      ++ai;
      llvm::Value* v = emitValueForParam(*arg, expected, expected_name);
      if (!v) {
        return nullptr;
      }
      call_args.push_back(v);
    }
    if (callee_fn->arg_size() != call_args.size()) {
      error(call.loc, "argument count mismatch calling " + recv_ty + "." + call.method);
      return nullptr;
    }
    if (callee_fn->getReturnType()->isVoidTy()) {
      builder_.CreateCall(callee_fn, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(callee_fn, call_args, is_static ? "static.call" : "mcall");
  }

  const auto& id = static_cast<const ast::IdentifierExpr&>(*object);
  auto lit = local_types_.find(id.name);
  auto vit = named_values_.find(id.name);

  // [dsl] Captured class receiver: inside a lambda `local_types_` only
  // holds params/locals, so a captured object is missing there. Resolve the
  // static type from `capture_types_` and fetch the live pointer from the ctx.
  if (lit == local_types_.end() && vit == named_values_.end() &&
      current_lambda_ctx_ && current_lambda_ctx_ty_) {
    auto cap = capture_slots_.find(id.name);
    auto cty = capture_types_.find(id.name);
    if (cap != capture_slots_.end() && cty != capture_types_.end()) {
      const std::string cap_ty = resolveTypeName(cty->second);
      if (ensureTypeAvailable(cap_ty) && isClassType(cap_ty)) {
        const ClassInfo* cinfo = findClass(cap_ty);
        if (cinfo) {
          llvm::Value* slot = builder_.CreateStructGEP(current_lambda_ctx_ty_,
                                                       current_lambda_ctx_, cap->second,
                                                       id.name + ".cap");
          llvm::Type* cty_llvm = llvmTypeForTypeName(cap_ty);
          llvm::Value* self = nullptr;
          if (current_lambda_ctx_by_value_) {
            llvm::Value* val = builder_.CreateLoad(cty_llvm, slot, id.name + ".capval");
            llvm::AllocaInst* tmp =
                createEntryAlloca(builder_.GetInsertBlock()->getParent(), cty_llvm,
                                  id.name + ".this");
            builder_.CreateStore(val, tmp);
            self = tmp;
          } else {
            self = builder_.CreateLoad(ptrTy(), slot, id.name + ".ptr");
          }
          if (self) {
            llvm::Value* callv = emitClassMethodCall(call, *cinfo, self, cap_ty);
            if (callv) {
              return callv;
            }
          }
        }
      }
    }
  }

  // Instance field receiver: `this.field.method(...)`. Fields are not locals, so
  // resolve them against the enclosing class before falling through to the
  // class-name / static / free-function path below.
  if ((lit == local_types_.end() || vit == named_values_.end()) &&
      !current_class_.empty() && current_this_ && !current_method_is_static_) {
    if (llvm::Value* callv = emitMethodCallOnField(call, id.name)) {
      return callv;
    }
  }

  // Namespace-qualified receiver: `net::http::Server.start()` — the chain names a
  // module scope and the member is one of its exported `pub` free functions.
  if (id.name.find("::") != std::string::npos) {
    return emitQualifiedFreeCall(call, id);
  }

  // `use net::http::server as http` retargets calls the parser attributed to the
  // pre-alias spelling (`http` / `server`) onto the full namespace scope.
  if (use_aliases_.count(id.name) != 0) {
    auto mit = module_exports_.find(expandAliasedScope(use_aliases_, id.name));
    if (mit != module_exports_.end()) {
      auto fit = mit->second.find(call.method);
      if (fit != mit->second.end() && fit->second) {
        return emitMethodAsFreeCall(*fit->second, call);
      }
    }
  }

  // ClassName.staticMethod / free assoc (`Variant.from_i32`) / package `math_lib.add`.
  if (lit == local_types_.end() || vit == named_values_.end()) {
    const bool is_class = ensureTypeAvailable(id.name) && isClassType(id.name);

    // Prefer real class static methods (no implicit this).
    if (is_class) {
      const ClassInfo* cinfo = findClass(id.name);
      const ast::FuncDeclStmt* static_ast = nullptr;
      if (cinfo && cinfo->ast) {
        for (const auto& m : cinfo->ast->methods) {
          if (m && m->name == call.method && m->is_static) {
            static_ast = m.get();
            break;
          }
        }
      }
      if (static_ast) {
        auto mit = cinfo->methods.find(call.method);
        if (mit == cinfo->methods.end() || !mit->second) {
          error(call.loc, "missing static method IR: " + id.name + "." + call.method);
          return nullptr;
        }
        llvm::Function* callee = mit->second;
        std::vector<const ast::Expr*> ordered;
        if (!resolveCallArgs(*static_ast, call.args, ordered)) {
          return nullptr;
        }
        if (callee->arg_size() != ordered.size()) {
          error(call.loc, "argument count mismatch calling " + id.name + "." + call.method);
          return nullptr;
        }
        std::vector<llvm::Value*> call_args;
        for (size_t i = 0; i < ordered.size(); ++i) {
          llvm::Type* expected =
              callee->getFunctionType()->getParamType(static_cast<unsigned>(i));
          std::string expected_name;
          if (i < static_ast->params.size()) {
            expected_name = resolveTypeName(static_ast->params[i].type_name);
          }
          llvm::Value* v = emitValueForParam(*ordered[i], expected, expected_name);
          if (!v) {
            return nullptr;
          }
          call_args.push_back(v);
        }
        if (callee->getReturnType()->isVoidTy()) {
          builder_.CreateCall(callee, call_args);
          return llvm::ConstantInt::get(i32Ty(), 0);
        }
        return builder_.CreateCall(callee, call_args, "static.call");
      }
    }

    auto fit = concrete_funcs_.find(call.method);
    if (!is_class) {
      if (fit == concrete_funcs_.end() || !fit->second) {
        error(call.loc, "unknown object in method call: " + id.name);
        return nullptr;
      }
      // Fall through as a free-function call under package/module syntax.
    } else {
      if (fit == concrete_funcs_.end() || !fit->second) {
        error(call.loc, "unknown associated function " + id.name + "." + call.method);
        return nullptr;
      }
    }
    const ast::FuncDeclStmt* fn_ast = fit->second;
    // Class-name dispatch: `Variant.from_i32` / `File.read_all` — return type may differ
    // from the class (factories vs static helpers).
    const std::string ir_name = resolveFuncIrName(fn_ast->name);
    if (ir_name.empty()) {
      error(call.loc, "unknown associated function " + id.name + "." + call.method);
      return nullptr;
    }
    llvm::Function* callee = module_->getFunction(ir_name);
    if (!callee) {
      error(call.loc, "missing associated function IR: " + ir_name);
      return nullptr;
    }
    std::vector<const ast::Expr*> ordered;
    if (!resolveCallArgs(*fn_ast, call.args, ordered)) {
      return nullptr;
    }
    if (callee->arg_size() != ordered.size()) {
      error(call.loc, "argument count mismatch calling " + id.name + "." + call.method);
      return nullptr;
    }
    std::vector<llvm::Value*> call_args;
    for (size_t i = 0; i < ordered.size(); ++i) {
      llvm::Type* expected = callee->getFunctionType()->getParamType(static_cast<unsigned>(i));
      std::string expected_name;
      if (i < fn_ast->params.size()) {
        expected_name = resolveTypeName(fn_ast->params[i].type_name);
      }
      llvm::Value* v = emitValueForParam(*ordered[i], expected, expected_name);
      if (!v) {
        return nullptr;
      }
      call_args.push_back(v);
    }
    if (callee->getReturnType()->isVoidTy()) {
      builder_.CreateCall(callee, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(callee, call_args, "assoc.call");
  }

  ensureTypeAvailable(lit->second);

  if (lit->second == "Arena") {
    return emitArenaMethod(id.name, call);
  }

  // Concrete class: direct call Class_method(this, ...)
  if (isClassType(lit->second)) {
    const ClassInfo* info = findClass(lit->second);
    if (!info) {
      error(call.loc, "unknown class in method call: " + lit->second);
      return nullptr;
    }

    // [link:42-Closure-Integration-Data-Race-Refinement]: Array.each + inline Lambda → while desugar (capturing-safe).
    if (call.method == "each" && info->template_name == "Array" && !call.args.empty() &&
        call.args.back().value && call.args.back().value->kind() == ast::ExprKind::Lambda) {
      return emitArrayEachInline(id.name, *info,
                                 static_cast<const ast::LambdaExpr&>(*call.args.back().value));
    }

    auto mit = info->methods.find(call.method);
    llvm::Function* callee_fn = nullptr;
    const ClassInfo* callee_info = info;
    llvm::Value* this_ptr = nullptr;  // outer object or delegate field GEP
    if (mit != info->methods.end()) {
      callee_fn = mit->second;
      callee_info = info;
    } else if (info->ast) {
      // [link:97-Implicit-Delegation]: promote from delegates; multiple hits → hard ambiguity error.
      struct DelegateHit {
        std::string type_name;
        unsigned field_idx = 0;
        llvm::Function* fn = nullptr;
        const ClassInfo* emb = nullptr;
      };
      std::vector<DelegateHit> hits;
      for (const auto& field : info->ast->fields) {
        if (!field || !field->is_delegate) {
          continue;
        }
        if (!ensureTypeAvailable(field->type_name) || !isClassType(field->type_name)) {
          continue;
        }
        const ClassInfo* emb = findClass(field->type_name);
        if (!emb) {
          continue;
        }
        auto emit = emb->methods.find(call.method);
        if (emit == emb->methods.end()) {
          continue;
        }
        const std::string phys = physicalFieldName(field->name);
        auto fit = info->field_index.find(phys);
        if (fit == info->field_index.end()) {
          error(call.loc, "delegate field layout missing for '" + field->type_name + "'");
          return nullptr;
        }
        hits.push_back(DelegateHit{field->type_name, fit->second, emit->second, emb});
      }
      if (hits.size() >= 2) {
        error(call.loc, "Ambiguous method '" + call.method + "': defined in both '" +
                            hits[0].type_name + "' and '" + hits[1].type_name + "'.");
        return nullptr;
      }
      if (hits.size() == 1) {
        llvm::Value* outer = classObjectPtr(id.name, id.loc);
        if (!outer) {
          return nullptr;
        }
        // `this` must be the delegated field address, not the outer object.
        this_ptr = builder_.CreateStructGEP(info->type, outer, hits[0].field_idx,
                                            "delegate." + hits[0].type_name);
        callee_fn = hits[0].fn;
        callee_info = hits[0].emb;
      }
    }
    if (!callee_fn) {
      error(call.loc, "unknown method " + lit->second + "." + call.method);
      return nullptr;
    }
    // Prefer signature reorder when the method AST is available.
    std::vector<const ast::Expr*> ordered;
    const ast::FuncDeclStmt* method_ast = nullptr;
    if (callee_info->ast) {
      for (const auto& m : callee_info->ast->methods) {
        if (m && m->name == call.method) {
          method_ast = m.get();
          break;
        }
      }
    }
    if (method_ast) {
      if (!resolveCallArgs(*method_ast, call.args, ordered)) {
        return nullptr;
      }
    } else {
      for (const ast::CallArg& a : call.args) {
        if (!a.name.empty()) {
          error(call.loc, "keyword arguments require a known method signature");
          return nullptr;
        }
        if (!a.value) {
          error(call.loc, "null argument in method call");
          return nullptr;
        }
        ordered.push_back(a.value.get());
      }
    }

    const bool is_static = method_ast && method_ast->is_static;
    std::vector<llvm::Value*> call_args;
    unsigned ai = 0;
    if (!is_static) {
      if (!this_ptr) {
        this_ptr = classObjectPtr(id.name, id.loc);
      }
      if (!this_ptr) {
        return nullptr;
      }
      call_args.push_back(this_ptr);
      ai = 1;
    }
    for (const ast::Expr* arg : ordered) {
      llvm::Type* expected = callee_fn->getFunctionType()->getParamType(ai);
      std::string expected_name;
      const size_t param_i = is_static ? static_cast<size_t>(ai) : static_cast<size_t>(ai - 1);
      if (method_ast && param_i < method_ast->params.size()) {
        expected_name =
            instantiateMemberType(method_ast->params[param_i].type_name, *callee_info);
      }
      ++ai;
      llvm::Value* v = emitValueForParam(*arg, expected, expected_name);
      if (!v) {
        return nullptr;
      }
      call_args.push_back(v);
    }
    if (callee_fn->arg_size() != call_args.size()) {
      error(call.loc, "argument count mismatch calling " + lit->second + "." + call.method);
      return nullptr;
    }
    if (callee_fn->getReturnType()->isVoidTy()) {
      builder_.CreateCall(callee_fn, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(callee_fn, call_args, is_static ? "static.call" : "mcall");
  }

  // Interface: fat-pointer vtable dispatch.
  if (isInterfaceType(lit->second)) {
    const InterfaceInfo* iface = findInterface(lit->second);
    auto mit = iface->method_index.find(call.method);
    if (mit == iface->method_index.end()) {
      error(call.loc, "unknown interface method " + lit->second + "." + call.method);
      return nullptr;
    }
    const unsigned slot = mit->second;
    const ast::FuncDeclStmt* sig = iface->methods.at(call.method);

    llvm::Value* fat = builder_.CreateLoad(fatPtrTy(), vit->second, id.name + ".fat");
    llvm::Value* data = builder_.CreateExtractValue(fat, 0, "iface.data");
    llvm::Value* vtable = builder_.CreateExtractValue(fat, 1, "iface.vtable");

    // vtable is ptr to [TypeID, method...]; GEP to method slot then load fn ptr.
    llvm::ArrayType* arr_ty = vtableArrayTy(*iface);
    llvm::Value* slot_ptr = builder_.CreateInBoundsGEP(
        arr_ty, vtable,
        {llvm::ConstantInt::get(i32Ty(), 0), llvm::ConstantInt::get(i32Ty(), slot)},
        "vtable.slot");
    llvm::Value* fn_ptr = builder_.CreateLoad(ptrTy(), slot_ptr, "fnptr");

    std::vector<llvm::Type*> param_tys;
    param_tys.push_back(ptrTy());
    for (const ast::Param& p : sig->params) {
      param_tys.push_back(abiTypeForParam(p.type_name));
    }
    llvm::Type* ret_ty = typeFromName(sig->return_type);
    auto* fty = llvm::FunctionType::get(ret_ty, param_tys, false);

    std::vector<llvm::Value*> call_args;
    call_args.push_back(data);
    for (size_t i = 0; i < call.args.size(); ++i) {
      if (!call.args[i].value) {
        return nullptr;
      }
      if (!call.args[i].name.empty()) {
        error(call.loc, "keyword arguments not supported on interface dispatch yet");
        return nullptr;
      }
      // Lower each argument against the interface signature. `toI32` here would
      // corrupt every non-integer formal (interface = fat pointer, f64, ...).
      llvm::Type* expected = i + 1 < param_tys.size() ? param_tys[i + 1] : nullptr;
      std::string expected_name;
      if (i < sig->params.size()) {
        expected_name = resolveTypeName(sig->params[i].type_name);
      }
      llvm::Value* v = emitValueForParam(*call.args[i].value, expected, expected_name);
      if (!v) {
        return nullptr;
      }
      call_args.push_back(v);
    }

    if (ret_ty->isVoidTy()) {
      builder_.CreateCall(fty, fn_ptr, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(fty, fn_ptr, call_args, "iface.call");
  }

  error(call.loc, "method call on non-object type");
  return nullptr;
}

llvm::Value* Codegen::emitArrayLiteral(const ast::ArrayLiteralExpr& expr) {
  const std::string arr_ty = inferTypeFromExpr(expr);
  if (arr_ty.empty()) {
    error(expr.loc, "cannot infer element type for array literal");
    return nullptr;
  }
  if (!ensureTypeAvailable(arr_ty)) {
    error(expr.loc, "unknown array type: " + arr_ty);
    return nullptr;
  }
  const ClassInfo* info = findClass(arr_ty);
  if (!info) {
    error(expr.loc, "unknown array type: " + arr_ty);
    return nullptr;
  }

  llvm::Function* cur = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, "arr.lit");

  // Lower `[a,b,c]` → `Array<T>()` then push each element.
  ast::NewExpr neu;
  neu.loc = expr.loc;
  neu.class_name = arr_ty;
  if (!initClassFromNew(tmp, *info, neu)) {
    return nullptr;
  }

  auto mit = info->methods.find("push");
  if (mit == info->methods.end()) {
    error(expr.loc, arr_ty + ".push not found");
    return nullptr;
  }
  const std::string elem_ty = info->type_args.empty() ? "i32" : info->type_args[0];
  for (const ast::ExprPtr& elem : expr.elements) {
    if (!elem) {
      error(expr.loc, "null element in array literal");
      return nullptr;
    }
    llvm::Type* expected = mit->second->getFunctionType()->getParamType(1);
    llvm::Value* v = emitValueForParam(*elem, expected, elem_ty);
    if (!v) {
      return nullptr;
    }
    builder_.CreateCall(mit->second, {tmp, v});
  }
  return tmp;
}

std::vector<std::string> Codegen::resolveCaptureLocals(const ast::LambdaExpr& expr) const {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  for (const std::string& name : expr.captures) {
    if (named_values_.count(name) == 0) {
      continue;  // global / const — no stack slot to capture
    }
    if (seen.insert(name).second) {
      out.push_back(name);
    }
  }
  return out;
}

// [dsl] Resolve `name { ... }` inside a config block to a live class
// object: a `get_name()` child when the class exposes one, otherwise the
// null-initialised `name_` field constructed in place.
llvm::Value* Codegen::emitConfigSectionPtr(const std::string& field_name,
                                          const std::string& class_name,
                                          const ast::SourceLocation& loc) {
  const ClassInfo* info = findClass(class_name);
  if (!info) {
    error(loc, "unknown config class '" + class_name + "'");
    return nullptr;
  }
  llvm::Value* self = current_this_;
  if (!self) {
    error(loc, "'" + field_name + " { ... }' is only valid inside a config block");
    return nullptr;
  }

  // `CPropertys` roots carry a name → node map, not typed fields: reuse the
  // existing child node when present, otherwise build a fresh one and attach it.
  if (class_name == "CPropertys") {
    if (info->methods.count("get_property") == 0 || info->methods.count("add_property") == 0) {
      error(loc, "CPropertys requires get_property/add_property for config sections");
      return nullptr;
    }
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();

    ast::MethodCallExpr get_call;
    get_call.loc = loc;
    get_call.method = "get_property";
    ast::CallArg get_key;
    auto key_lit = std::make_unique<ast::StringLiteralExpr>();
    key_lit->loc = loc;
    key_lit->value = field_name;
    get_key.value = std::move(key_lit);
    get_call.args.push_back(std::move(get_key));
    llvm::Value* found = emitClassMethodCall(get_call, *info, self, class_name);
    if (!found) {
      return nullptr;
    }

    // Optional<CPropertys>: hit → the node handle, miss → attach a new node.
    if (!found->getType()->isPointerTy()) {
      error(loc, "get_property did not return an Optional handle");
      return nullptr;
    }
    const ClassInfo* opt_info = findClass("Optional<CPropertys>");
    if (!opt_info || !opt_info->type) {
      error(loc, "Optional<CPropertys> is not available");
      return nullptr;
    }
    llvm::StructType* opt_ty = opt_info->type;
    llvm::Value* hit = builder_.CreateLoad(opt_ty, found, "section.opt");
    llvm::Value* has = builder_.CreateExtractValue(hit, 0, "section.has");

    llvm::BasicBlock* hit_bb = llvm::BasicBlock::Create(globalContext(), "section.hit", fn);
    llvm::BasicBlock* miss_bb = llvm::BasicBlock::Create(globalContext(), "section.miss", fn);
    llvm::BasicBlock* join_bb = llvm::BasicBlock::Create(globalContext(), "section.join", fn);
    builder_.CreateCondBr(has, hit_bb, miss_bb);

    builder_.SetInsertPoint(hit_bb);
    llvm::Value* existing = builder_.CreateExtractValue(hit, 1, "section.node");
    builder_.CreateBr(join_bb);

    builder_.SetInsertPoint(miss_bb);
    const ClassInfo* node_info = findClass("CPropertys");
    if (!node_info) {
      error(loc, "CPropertys is not available");
      return nullptr;
    }
    llvm::AllocaInst* fresh = createEntryAlloca(fn, node_info->type, "section.node.fresh");
    if (!initClassAtPtr(fresh, *node_info, ast::NewExpr{})) {
      return nullptr;
    }
    ast::MethodCallExpr add_call;
    add_call.loc = loc;
    add_call.method = "add_property";
    ast::CallArg add_key;
    auto add_lit = std::make_unique<ast::StringLiteralExpr>();
    add_lit->loc = loc;
    add_lit->value = field_name;
    add_key.value = std::move(add_lit);
    add_call.args.push_back(std::move(add_key));
    // `add_property(key, node)` — a class argument is passed by reference.
    ast::CallArg node_arg;
    auto node_ref = std::make_unique<ast::IdentifierExpr>();
    node_ref->loc = loc;
    node_ref->name = "__section_node";
    node_arg.value = std::move(node_ref);
    add_call.args.push_back(std::move(node_arg));
    const std::string saved_local = "__section_node";
    local_types_[saved_local] = "CPropertys";
    named_values_[saved_local] = fresh;
    llvm::Value* added = emitClassMethodCall(add_call, *info, self, class_name);
    named_values_.erase(saved_local);
    local_types_.erase(saved_local);
    if (!added) {
      return nullptr;
    }
    builder_.CreateBr(join_bb);

    builder_.SetInsertPoint(join_bb);
    llvm::PHINode* phi = builder_.CreatePHI(ptrTy(), 2, "section.ptr");
    phi->addIncoming(existing, hit_bb);
    phi->addIncoming(fresh, miss_bb);
    return phi;
  }

  const std::string getter = "get_" + field_name;
  auto git = info->methods.find(getter);
  if (git != info->methods.end()) {
    ast::MethodCallExpr call;
    call.loc = loc;
    call.method = getter;
    return emitClassMethodCall(call, *info, self, class_name);
  }

  const std::string phys = physicalFieldName(field_name);
  auto fit = info->field_index.find(phys);
  if (fit == info->field_index.end()) {
    error(loc, "unknown section '" + field_name + "' on '" + class_name + "'");
    return nullptr;
  }
  return builder_.CreateStructGEP(info->type, self, fit->second, field_name + ".section");
}

// [dsl] Emit a bare-brace config block inline at its call site: `this`
// points at `root`, bare `name = v` became `this.name = v`, and nested
// `section { ... }` blocks rebind `this` to the section field.
bool Codegen::emitConfigBlockInline(const ast::LambdaExpr& body, llvm::Value* root,
                                    const std::string& class_name) {
  const std::string saved_class = current_class_;
  llvm::Value* saved_this = current_this_;
  const bool saved_static = current_method_is_static_;

  current_class_ = class_name;
  current_this_ = root;
  current_method_is_static_ = false;

  // [dsl] The block is not a closure: it runs inline, so `this` is just
  // the root receiver. Nothing extra needs to be captured or restored.
  enterDeferScope();
  bool ok = emitStmtList(body.body);
  if (ok) {
    ok = flushCurrentScopeDefers();
  }
  exitDeferScope();

  current_class_ = saved_class;
  current_this_ = saved_this;
  current_method_is_static_ = saved_static;
  return ok;
}

llvm::Value* Codegen::emitLambda(const ast::LambdaExpr& expr, const std::string& fun_type_name) {
  const std::string resolved = resolveTypeName(fun_type_name);
  std::vector<std::string> param_tys;
  std::string ret;
  bool has_ret = false;
  if (!parseFunTypeName(resolved, param_tys, ret, has_ret)) {
    error(expr.loc, "invalid function type for lambda: " + fun_type_name);
    return nullptr;
  }
  if (expr.params.size() != param_tys.size()) {
    error(expr.loc, "lambda parameter count does not match " + resolved);
    return nullptr;
  }

  const std::vector<std::string> caps = resolveCaptureLocals(expr);
  const bool capturing = !caps.empty();

  llvm::Type* ret_ty =
      (!has_ret || ret == "void") ? voidTy() : llvmTypeForTypeName(ret);

  std::vector<llvm::Type*> fn_params;
  if (capturing) {
    fn_params.push_back(ptrTy());  // ctx*
  }
  for (const std::string& p : param_tys) {
    fn_params.push_back(abiTypeForParam(p));
  }
  auto* fty = llvm::FunctionType::get(ret_ty, fn_params, /*isVarArg=*/false);

  const std::string fname = "__lambda_" + std::to_string(++lambda_seq_);
  llvm::Function* fn =
      llvm::Function::Create(fty, llvm::Function::InternalLinkage, fname, module_.get());

  // Snapshot outer locals needed to fill the context after the body is emitted.
  std::unordered_map<std::string, llvm::Value*> outer_slots;
  std::unordered_map<std::string, std::string> outer_cap_types;
  for (const std::string& name : caps) {
    outer_slots[name] = named_values_.at(name);
    auto lit = local_types_.find(name);
    outer_cap_types[name] = lit != local_types_.end() ? lit->second : "i32";
  }

  llvm::StructType* ctx_ty = nullptr;
  if (capturing) {
    std::vector<llvm::Type*> fields;
    fields.reserve(caps.size());
    if (expr.escapes) {
      // Heap-promoted captures live by value inside the ctx block.
      for (const std::string& name : caps) {
        fields.push_back(localSlotType(outer_slots[name]));
      }
    } else {
      fields.assign(caps.size(), ptrTy());
    }
    ctx_ty = llvm::StructType::create(globalContext(), fields,
                                      "__LambdaCtx_" + std::to_string(++lambda_ctx_seq_));
  }

  auto saved_named = named_values_;
  auto saved_locals = local_types_;
  auto saved_heap = heap_locals_;
  auto saved_owned_strings = owned_string_locals_;
  auto saved_readonly = readonly_refs_;
  auto saved_defers = defer_scopes_;
  auto saved_autofree = auto_free_scopes_;
  auto saved_class = current_class_;
  llvm::Value* saved_this = current_this_;
  auto saved_cap_slots = capture_slots_;
  auto saved_cap_types = capture_types_;
  llvm::Value* saved_lam_ctx = current_lambda_ctx_;
  llvm::StructType* saved_lam_ctx_ty = current_lambda_ctx_ty_;
  const bool saved_lam_by_value = current_lambda_ctx_by_value_;
  llvm::BasicBlock* saved_insert = builder_.GetInsertBlock();
  llvm::BasicBlock::iterator saved_point =
      saved_insert ? builder_.GetInsertPoint() : llvm::BasicBlock::iterator();

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  named_values_.clear();
  local_types_.clear();
  heap_locals_.clear();
  owned_string_locals_.clear();
  arena_locals_.clear();
  readonly_refs_.clear();
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  capture_slots_.clear();
  capture_types_.clear();
  enterDeferScope();
  current_class_.clear();
  current_this_ = nullptr;

  unsigned arg_i = 0;
  if (capturing) {
    llvm::Argument& ctx_arg = *fn->arg_begin();
    ctx_arg.setName("ctx");
    current_lambda_ctx_ = &ctx_arg;
    current_lambda_ctx_ty_ = ctx_ty;
    current_lambda_ctx_by_value_ = expr.escapes;
    for (unsigned i = 0; i < caps.size(); ++i) {
      capture_slots_[caps[i]] = i;
      capture_types_[caps[i]] = outer_cap_types[caps[i]];
    }
    ++arg_i;
  } else {
    current_lambda_ctx_ = nullptr;
    current_lambda_ctx_ty_ = nullptr;
    current_lambda_ctx_by_value_ = false;
  }

  for (size_t pi = 0; pi < expr.params.size(); ++pi, ++arg_i) {
    llvm::Argument& arg = *std::next(fn->arg_begin(), static_cast<int>(arg_i));
    arg.setName(expr.params[pi]);
    ast::Param p;
    p.name = expr.params[pi];
    p.type_name = param_tys[pi];
    p.is_mutable = false;
    p.loc = expr.loc;
    bindParam(fn, &arg, p, resolveTypeName(param_tys[pi]));
  }

  bool ok = emitStmtList(expr.body);
  if (ok && builder_.GetInsertBlock() && !builder_.GetInsertBlock()->getTerminator()) {
    if (!finishCleanups()) {
      ok = false;
    } else if (fn->getReturnType()->isVoidTy()) {
      builder_.CreateRetVoid();
    } else if (fn->getReturnType()->isIntegerTy(32)) {
      builder_.CreateRet(llvm::ConstantInt::get(i32Ty(), 0));
    } else {
      error(expr.loc, "missing return in lambda");
      ok = false;
    }
  }
  defer_scopes_.clear();
  auto_free_scopes_.clear();

  named_values_ = std::move(saved_named);
  local_types_ = std::move(saved_locals);
  heap_locals_ = std::move(saved_heap);
  owned_string_locals_ = std::move(saved_owned_strings);
  readonly_refs_ = std::move(saved_readonly);
  defer_scopes_ = std::move(saved_defers);
  auto_free_scopes_ = std::move(saved_autofree);
  current_class_ = std::move(saved_class);
  current_this_ = saved_this;
  capture_slots_ = std::move(saved_cap_slots);
  capture_types_ = std::move(saved_cap_types);
  current_lambda_ctx_ = saved_lam_ctx;
  current_lambda_ctx_ty_ = saved_lam_ctx_ty;
  current_lambda_ctx_by_value_ = saved_lam_by_value;
  if (saved_insert) {
    builder_.SetInsertPoint(saved_insert, saved_point);
  }

  if (!ok || llvm::verifyFunction(*fn, &llvm::errs())) {
    error(expr.loc, "lambda verification failed");
    fn->eraseFromParent();
    return nullptr;
  }

  pending_closure_ctx_ = nullptr;
  pending_closure_ctx_ty_ = nullptr;
  if (capturing) {
    if (!saved_insert) {
      error(expr.loc, "capturing lambda requires an enclosing function");
      return nullptr;
    }
    if (expr.escapes) {
      // [link:75-Escape-Analysis-Auto-Heap-Promotion]: escaping closure — heap-allocate
      // ctx and copy capture values into it.
      llvm::Function* alloc_fn = requireExtern("l_alloc", expr.loc);
      if (!alloc_fn) {
        return nullptr;
      }
      ensureDataLayout();
      const uint64_t ctx_bytes = module_->getDataLayout().getTypeAllocSize(ctx_ty);
      llvm::Value* ctx_sz =
          llvm::ConstantInt::get(i64Ty(), static_cast<uint64_t>(ctx_bytes), /*isSigned=*/false);
      llvm::Value* ctx_heap = builder_.CreateCall(alloc_fn, {ctx_sz}, "lambda.ctx.heap");
      for (unsigned i = 0; i < caps.size(); ++i) {
        llvm::Value* slot = outer_slots[caps[i]];
        llvm::Value* val =
            builder_.CreateLoad(localSlotType(slot), slot, caps[i] + ".esc.ld");
        llvm::Value* gep =
            builder_.CreateStructGEP(ctx_ty, ctx_heap, i, caps[i] + ".slot");
        builder_.CreateStore(val, gep);
      }
      pending_closure_ctx_ = ctx_heap;
      pending_closure_ctx_ty_ = ctx_ty;
    } else {
      // Non-escaping: stack ctx pointing at stack allocas (Array.each, local closures).
      llvm::Function* outer = saved_insert->getParent();
      llvm::AllocaInst* ctx_alloca =
          createEntryAlloca(outer, ctx_ty, "lambda.ctx");
      for (unsigned i = 0; i < caps.size(); ++i) {
        llvm::Value* gep =
            builder_.CreateStructGEP(ctx_ty, ctx_alloca, i, caps[i] + ".slot");
        builder_.CreateStore(outer_slots[caps[i]], gep);
      }
      pending_closure_ctx_ = ctx_alloca;
      pending_closure_ctx_ty_ = ctx_ty;
    }
  }
  return fn;
}

llvm::Value* Codegen::emitExpr(const ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::Match: {
      return emitMatchExpr(static_cast<const ast::MatchExpr&>(expr));
    }
    case ast::ExprKind::IntLiteral: {
      const auto& lit = static_cast<const ast::IntLiteralExpr&>(expr);
      long long n = 0;
      try {
        n = std::stoll(lit.value);
      } catch (...) {
        error(expr.loc, "invalid integer literal: " + lit.value);
        return nullptr;
      }
      // Preserve full width when the literal does not fit in signed i32 (e.g. i64 max).
      if (n < static_cast<long long>(INT32_MIN) || n > static_cast<long long>(INT32_MAX)) {
        return llvm::ConstantInt::get(i64Ty(), static_cast<uint64_t>(n), /*isSigned=*/true);
      }
      return llvm::ConstantInt::get(i32Ty(), static_cast<uint64_t>(n), /*isSigned=*/true);
    }
    case ast::ExprKind::FloatLiteral: {
      const auto& lit = static_cast<const ast::FloatLiteralExpr&>(expr);
      double d = 0.0;
      try {
        d = std::stod(lit.value);
      } catch (...) {
        error(expr.loc, "invalid float literal: " + lit.value);
        return nullptr;
      }
      return llvm::ConstantFP::get(f64Ty(), d);
    }
    case ast::ExprKind::BoolLiteral: {
      const auto& lit = static_cast<const ast::BooleanLiteralExpr&>(expr);
      return lit.value ? llvm::ConstantInt::getTrue(globalContext())
                       : llvm::ConstantInt::getFalse(globalContext());
    }
    case ast::ExprKind::This: {
      if (current_method_is_static_) {
        error(expr.loc, "cannot use 'this' inside a static method");
        return nullptr;
      }
      if (!current_this_) {
        error(expr.loc, "'this' is only valid inside an instance method");
        return nullptr;
      }
      return current_this_;
    }
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);

      // L-side compile-time introspection: fold to constants (no runtime calls).
      if (id.name == "__FILE__") {
        const std::string& file =
            id.loc.filename.empty() ? std::string("<unknown>") : id.loc.filename;
        return makeStringLitObject(file, "ctx.file", id.loc);
      }
      if (id.name == "__FUNC__") {
        const std::string& fn =
            current_fn_name_.empty() ? std::string("<unknown>") : current_fn_name_;
        return makeStringLitObject(fn, "ctx.func", id.loc);
      }
      if (id.name == "__LINE__") {
        return llvm::ConstantInt::get(i32Ty(), static_cast<uint64_t>(id.loc.line));
      }

      // Compile-time const: inline the Constant (no load).
      auto cit = const_values_.find(id.name);
      if (cit != const_values_.end()) {
        return cit->second;
      }

      // Captured local via closure context.
      // Non-escape: load ptr from ctx, then load value.
      // Escape (by-value): load value directly from the heap-promoted ctx field.
      auto cap = capture_slots_.find(id.name);
      if (cap != capture_slots_.end() && current_lambda_ctx_ && current_lambda_ctx_ty_) {
        llvm::Value* slot = builder_.CreateStructGEP(current_lambda_ctx_ty_, current_lambda_ctx_,
                                                     cap->second, id.name + ".cap");
        auto ty_it = capture_types_.find(id.name);
        const std::string ty_name = ty_it != capture_types_.end() ? ty_it->second : "i32";
        llvm::Type* vty = llvmTypeForTypeName(ty_name);
        if (current_lambda_ctx_by_value_) {
          return builder_.CreateLoad(vty, slot, id.name);
        }
        llvm::Value* local_ptr = builder_.CreateLoad(ptrTy(), slot, id.name + ".ptr");
        return builder_.CreateLoad(vty, local_ptr, id.name);
      }

      if (id.name == "this" && current_method_is_static_) {
        error(expr.loc, "cannot use 'this' inside a static method");
        return nullptr;
      }
      // Method body: field load via this (logical → physical).
      if (!current_class_.empty()) {
        if (llvm::Value* field = loadFieldOnThis(id.name)) {
          return field;
        }
      }

      auto it = named_values_.find(id.name);
      if (it != named_values_.end()) {
        llvm::Value* alloca = it->second;
        llvm::Type* ty = localSlotType(alloca);
        auto lit = local_types_.find(id.name);
        if (lit != local_types_.end() && isClassType(lit->second)) {
          return classObjectPtr(id.name, id.loc);
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

      auto git = global_vars_.find(id.name);
      if (git != global_vars_.end()) {
        llvm::GlobalVariable* gv = git->second;
        auto gty = global_types_.find(id.name);
        if (gty != global_types_.end() && isClassType(gty->second)) {
          // Match locals: class lvalues are addresses (for property / method sugar).
          return gv;
        }
        return builder_.CreateLoad(gv->getValueType(), gv, id.name);
      }

      error(expr.loc, "unknown variable: " + id.name);
      return nullptr;
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
      if (access.is_safe) {
        return emitSafeMemberAccess(access);
      }
      if (!access.object) {
        error(expr.loc, "member access missing object");
        return nullptr;
      }
      std::string obj_ty = exprTypeOf(*access.object);
      if (obj_ty.empty()) {
        obj_ty = inferTypeFromExpr(*access.object);
      }
      if (isClassType(obj_ty)) {
        ensureTypeAvailable(obj_ty);
        const ClassInfo* info = findClass(obj_ty);
        if (!info) {
          error(expr.loc, "unknown class in member access: " + obj_ty);
          return nullptr;
        }
        const PropResolve pr = resolveProperty(*info, access.member, PropAccess::Read);
        if (pr.kind == PropResolveKind::Method) {
          auto mit = info->methods.find(pr.method_name);
          if (mit == info->methods.end()) {
            error(expr.loc, "missing getter " + pr.method_name);
            return nullptr;
          }
          llvm::Value* self = classValuePtr(*access.object, obj_ty);
          if (!self) {
            return nullptr;
          }
          if (mit->second->getReturnType()->isVoidTy()) {
            builder_.CreateCall(mit->second, {self});
            return llvm::ConstantInt::get(i32Ty(), 0);
          }
          return builder_.CreateCall(mit->second, {self}, "prop.get");
        }
        if (pr.kind != PropResolveKind::Field) {
          error(expr.loc, "unknown property '" + access.member + "' (no get_" +
                                access.member + " / pub " + access.member + "_)");
          return nullptr;
        }
        auto fit = info->field_index.find(pr.field_name);
        llvm::Value* base = classValuePtr(*access.object, obj_ty);
        if (!base) {
          return nullptr;
        }
        llvm::Value* gep =
            builder_.CreateStructGEP(info->type, base, fit->second, pr.field_name);
        const std::string& fty_name = info->field_types[fit->second];
        if (isClassType(fty_name)) {
          return gep;
        }
        if (isInterfaceType(fty_name)) {
          return builder_.CreateLoad(fatPtrTy(), gep, pr.field_name);
        }
        return builder_.CreateLoad(llvmTypeForTypeName(fty_name), gep, pr.field_name);
      }
      llvm::Value* ptr = emitLValue(access);
      if (!ptr) {
        return nullptr;
      }
      return builder_.CreateLoad(i32Ty(), ptr, access.member);
    }
    case ast::ExprKind::Cast:
      return emitCast(static_cast<const ast::CastExpr&>(expr));
    case ast::ExprKind::IncDec:
      return emitIncDec(static_cast<const ast::IncDecExpr&>(expr));
    case ast::ExprKind::Try:
      return emitTry(static_cast<const ast::TryExpr&>(expr));
    case ast::ExprKind::Spawn: {
      const auto& sp = static_cast<const ast::SpawnExpr&>(expr);
      return emitSpawnBody(sp.body, sp.loc);
    }
    case ast::ExprKind::AddrOf:
      return emitAddrOf(static_cast<const ast::AddrOfExpr&>(expr));
    case ast::ExprKind::Await:
      return emitAwaitExpr(static_cast<const ast::AwaitExpr&>(expr));
    case ast::ExprKind::MethodCall:
      return emitMethodCall(static_cast<const ast::MethodCallExpr&>(expr));
    case ast::ExprKind::ScopedCall:
      return emitScopedCall(static_cast<const ast::ScopedCallExpr&>(expr));
    case ast::ExprKind::Index: {
      const auto& ix = static_cast<const ast::IndexExpr&>(expr);
      llvm::Value* ptr = emitIndexLValue(ix);
      if (!ptr) {
        return nullptr;
      }
      llvm::Type* elem_ty = nullptr;
      if (ix.object) {
        elem_ty = elementTypeForIndexedBase(*ix.object);
      }
      if (!elem_ty) {
        elem_ty = elementTypeForPtrIndex();
      }
      if (!elem_ty && ix.object && ix.object->kind() == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*ix.object);
        auto lit = local_types_.find(id.name);
        if (lit != local_types_.end() && isClassType(lit->second)) {
          const ClassInfo* info = findClass(lit->second);
          if (info && !info->type_args.empty()) {
            elem_ty = llvmTypeForTypeName(info->type_args[0]);
          }
        }
      }
      if (!elem_ty) {
        error(expr.loc, "cannot load index expression: unknown element type");
        return nullptr;
      }
      return builder_.CreateLoad(elem_ty, ptr, "idx.load");
    }
    case ast::ExprKind::Sizeof:
      return emitSizeof(static_cast<const ast::SizeofExpr&>(expr));
    case ast::ExprKind::ArrayLiteral:
      return emitArrayLiteral(static_cast<const ast::ArrayLiteralExpr&>(expr));
    case ast::ExprKind::Lambda: {
      // Standalone / inferred context: Phase-1 default fun(i32...)->i32.
      const std::string fun_ty = inferTypeFromExpr(expr);
      return emitLambda(static_cast<const ast::LambdaExpr&>(expr), fun_ty);
    }
    case ast::ExprKind::New: {
      const auto& neu = static_cast<const ast::NewExpr&>(expr);
      if (!ensureTypeAvailable(neu.class_name)) {
        error(expr.loc, "unknown class in NewExpr: " + neu.class_name);
        return nullptr;
      }
      const ClassInfo* info = findClass(neu.class_name);
      if (!info) {
        error(expr.loc, "unknown class in NewExpr: " + neu.class_name);
        return nullptr;
      }
      llvm::Function* fn = builder_.GetInsertBlock()->getParent();
      llvm::AllocaInst* tmp = createEntryAlloca(fn, info->type, neu.class_name + ".tmp");
      if (!initClassFromNew(tmp, *info, neu)) {
        return nullptr;
      }
      return tmp;
    }
    case ast::ExprKind::Coalesce: {
      // `opt ?? fallback` — evaluate `opt` ONCE into a temporary, branch on
      // `is_some()`, then select `unwrap()` or the fallback. The temporary is
      // registered as a local so method dispatch can take its address.
      const auto& co = static_cast<const ast::CoalesceExpr&>(expr);
      if (!co.left || !co.right) {
        error(expr.loc, "?? requires both operands");
        return nullptr;
      }

      const std::string left_ty = exprTypeOf(*co.left);
      if (left_ty.rfind("Optional<", 0) != 0) {
        error(expr.loc, "left operand of '??' must be Optional<T>, got '" + left_ty + "'");
        return nullptr;
      }
      if (!ensureTypeAvailable(left_ty)) {
        return nullptr;
      }
      const ClassInfo* opt_info = findClass(left_ty);
      if (!opt_info || !opt_info->type) {
        error(expr.loc, "unknown Optional type in '??': " + left_ty);
        return nullptr;
      }

      llvm::Value* opt = emitExpr(*co.left);
      if (!opt) {
        return nullptr;
      }
      // Class values travel as pointers to their storage; normalize to a value.
      if (opt->getType()->isPointerTy()) {
        opt = builder_.CreateLoad(opt_info->type, opt, "coal.opt");
      }
      if (opt->getType() != opt_info->type) {
        error(expr.loc, "'??' left operand has unexpected representation in " + left_ty);
        return nullptr;
      }
      // Materialize the Optional once; the temp name identifies the alloca.
      const std::string tmp_name = "__coalesce_" + std::to_string(coalesce_tmp_counter_++);
      llvm::Function* cur_fn = builder_.GetInsertBlock()->getParent();
      llvm::AllocaInst* tmp_slot = createEntryAlloca(cur_fn, opt_info->type, tmp_name);
      builder_.CreateStore(opt, tmp_slot);
      local_types_[tmp_name] = left_ty;
      named_values_[tmp_name] = tmp_slot;

      // Presence test: Optional.is_some()
      auto make_opt_ref = [&]() {
        auto id = std::make_unique<ast::IdentifierExpr>();
        id->loc = co.left->loc;
        id->name = tmp_name;
        return id;
      };
      ast::MethodCallExpr is_some_call;
      is_some_call.loc = co.loc;
      is_some_call.object = make_opt_ref();
      is_some_call.method = "is_some";
      llvm::Value* has = emitMethodCall(is_some_call);
      if (!has) {
        named_values_.erase(tmp_name);
        local_types_.erase(tmp_name);
        return nullptr;
      }

      llvm::Function* fn = builder_.GetInsertBlock()->getParent();
      llvm::BasicBlock* some_bb = llvm::BasicBlock::Create(globalContext(), "coal.some", fn);
      llvm::BasicBlock* none_bb = llvm::BasicBlock::Create(globalContext(), "coal.none", fn);
      llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(globalContext(), "coal.merge", fn);

      builder_.CreateCondBr(toI1(has), some_bb, none_bb);

      // Payload type T of Optional<T>. Class values travel as POINTERS in this
      // backend, so the canonical arm representation is a pointer for classes
      // and a scalar for primitives.
      std::string payload_ty = left_ty.substr(std::string("Optional<").size());
      if (!payload_ty.empty() && payload_ty.back() == '>') {
        payload_ty.pop_back();
      }
      const bool payload_is_class =
          payload_ty == "String" || isClassType(payload_ty);
      llvm::Type* payload_llvm_ty = payload_is_class ? nullptr : typeFromName(payload_ty);
      const ClassInfo* payload_info = payload_is_class ? findClass(payload_ty) : nullptr;

      auto normalize_arm = [&](llvm::Value* v) -> llvm::Value* {
        if (payload_is_class) {
          if (v->getType()->isPointerTy()) {
            return v;
          }
          if (payload_info && payload_info->type && v->getType() == payload_info->type) {
            llvm::Function* cf = builder_.GetInsertBlock()->getParent();
            llvm::AllocaInst* slot =
                createEntryAlloca(cf, payload_info->type, "coal.cls");
            builder_.CreateStore(v, slot);
            return slot;
          }
          return v;
        }
        llvm::Type* want = payload_llvm_ty;
        if (!want || !want->isSized()) {
          return v;
        }
        if (v->getType() == want) {
          return v;
        }
        if (v->getType()->isPointerTy()) {
          // `ptr` literal (e.g. a string C buffer) is not a valid scalar payload.
          return v;
        }
        if (v->getType()->isIntegerTy() && want->isIntegerTy()) {
          return builder_.CreateIntCast(v, want, /*isSigned=*/true, "coal.cast");
        }
        if (v->getType()->isDoubleTy() && want->isFloatTy()) {
          return builder_.CreateFPTrunc(v, want, "coal.cast");
        }
        if (v->getType()->isFloatTy() && want->isDoubleTy()) {
          return builder_.CreateFPExt(v, want, "coal.cast");
        }
        if (v->getType()->isIntegerTy() && want->isFloatingPointTy()) {
          return builder_.CreateSIToFP(v, want, "coal.cast");
        }
        if (v->getType()->isFloatingPointTy() && want->isIntegerTy()) {
          return builder_.CreateFPToSI(v, want, "coal.cast");
        }
        return v;
      };

      // Some arm: unwrap() then normalize — inside `some_bb` so the PHI incoming
      // dominates its own predecessor block.
      builder_.SetInsertPoint(some_bb);
      ast::MethodCallExpr unwrap_call;
      unwrap_call.loc = co.loc;
      unwrap_call.object = make_opt_ref();
      unwrap_call.method = "unwrap";
      llvm::Value* some_val = emitMethodCall(unwrap_call);
      if (!some_val) {
        named_values_.erase(tmp_name);
        local_types_.erase(tmp_name);
        return nullptr;
      }
      some_val = normalize_arm(some_val);
      llvm::BasicBlock* some_end = builder_.GetInsertBlock();
      builder_.CreateBr(merge_bb);

      // None arm: the fallback expression.
      builder_.SetInsertPoint(none_bb);
      llvm::Value* none_val = nullptr;
      // `Optional<Variant> ?? <primitive|String>`: implicitly box the fallback so
      // both arms merge as `%Variant` values (read-side counterpart to auto-boxing).
      if (resolveTypeName(payload_ty) == "Variant") {
        none_val = emitImplicitVariantBox(*co.right, nullptr);
      }      if (!none_val) {
        if (payload_is_class && co.right->kind() == ast::ExprKind::StringLiteral) {
          none_val = emitStringObjectPtr(*co.right);
        } else {
          none_val = emitExpr(*co.right);
        }
      }
      if (!none_val) {
        named_values_.erase(tmp_name);
        local_types_.erase(tmp_name);
        return nullptr;
      }
      none_val = normalize_arm(none_val);
      llvm::BasicBlock* none_end = builder_.GetInsertBlock();
      builder_.CreateBr(merge_bb);

      builder_.SetInsertPoint(merge_bb);
      if (some_val->getType() != none_val->getType()) {
        std::string some_desc;
        std::string none_desc;
        llvm::raw_string_ostream so(some_desc);
        llvm::raw_string_ostream no(none_desc);
        some_val->getType()->print(so);
        none_val->getType()->print(no);
        error(expr.loc, "'??' arms have mismatched types: some=" + so.str() + " none=" + no.str());
        named_values_.erase(tmp_name);
        local_types_.erase(tmp_name);
        return nullptr;
      }
      llvm::PHINode* phi = builder_.CreatePHI(some_val->getType(), 2, "coalesce");
      phi->addIncoming(some_val, some_end);
      phi->addIncoming(none_val, none_end);

      named_values_.erase(tmp_name);
      local_types_.erase(tmp_name);
      return phi;
    }
    case ast::ExprKind::Binary: {
      const auto& bin = static_cast<const ast::BinaryExpr&>(expr);

      if (bin.op == "&&" || bin.op == "||") {        llvm::Function* fn = builder_.GetInsertBlock()->getParent();
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

      // String / literal / interpolation `+` (i32 auto-coerced via l_int_to_str).
      if (isStringConcatBinary(bin)) {
        return emitStringAdd(bin);
      }

      llvm::Value* lhs = emitExpr(*bin.left);
      llvm::Value* rhs = emitExpr(*bin.right);
      if (!lhs || !rhs) {
        return nullptr;
      }

      const std::string lt = exprTypeOf(*bin.left);
      const std::string rt = exprTypeOf(*bin.right);

      if (bin.op == "+" || bin.op == "-" || bin.op == "*" || bin.op == "/" ||
          bin.op == "%") {
        std::string left_ty = lt;
        std::string right_ty = rt;
        // Legacy fallback: untyped field / sizeof-style operands default to i32.
        if (left_ty.empty() && (lhs->getType()->isIntegerTy() || lhs->getType()->isDoubleTy())) {
          left_ty = lhs->getType()->isDoubleTy() ? "f64"
                   : (lhs->getType()->isIntegerTy(64) ? "i64" : "i32");
        }
        if (right_ty.empty() &&
            (rhs->getType()->isIntegerTy() || rhs->getType()->isDoubleTy())) {
          right_ty = rhs->getType()->isDoubleTy() ? "f64"
                     : (rhs->getType()->isIntegerTy(64) ? "i64" : "i32");
        }
        std::string promoted = promoteNumericTypes(left_ty, right_ty);
        if (promoted.empty()) {
          error(expr.loc, "incompatible types for '" + bin.op + "': '" + lt + "' and '" + rt +
                             "'");
          return nullptr;
        }
        lhs = emitNumericCast(lhs, left_ty, promoted, bin.loc);
        rhs = emitNumericCast(rhs, right_ty, promoted, bin.loc);
        if (!lhs || !rhs) {
          return nullptr;
        }
        if (promoted == "f64") {
          if (bin.op == "+") return builder_.CreateFAdd(lhs, rhs, "faddtmp");
          if (bin.op == "-") return builder_.CreateFSub(lhs, rhs, "fsubtmp");
          if (bin.op == "*") return builder_.CreateFMul(lhs, rhs, "fmultmp");
          if (bin.op == "%") return builder_.CreateFRem(lhs, rhs, "fremtmp");
          return builder_.CreateFDiv(lhs, rhs, "fdivtmp");
        }
        if (bin.op == "+") return builder_.CreateAdd(lhs, rhs, "addtmp");
        if (bin.op == "-") return builder_.CreateSub(lhs, rhs, "subtmp");
        if (bin.op == "*") return builder_.CreateMul(lhs, rhs, "multmp");
        if (bin.op == "%") return builder_.CreateSRem(lhs, rhs, "remtmp");
        return builder_.CreateSDiv(lhs, rhs, "divtmp");
      }

      // C-string / String equality via l_str_eq (e.g. get_build_mode() == "Debug").
      // Trigger when either side is a string literal / String (not bare ptr==ptr).
      if ((bin.op == "==" || bin.op == "!=") &&
          (isStringOperand(*bin.left) || isStringOperand(*bin.right) || lt == "String" ||
           rt == "String") &&
          !isNumericType(lt) && !isNumericType(rt)) {
        llvm::Value* a = stringOperandToCStr(lhs, lt, *bin.left);
        llvm::Value* b = stringOperandToCStr(rhs, rt, *bin.right);
        if (!a || !b) {
          return nullptr;
        }
        llvm::Function* str_eq = requireExtern("l_str_eq", bin.loc);
        if (!str_eq) {
          return nullptr;
        }
        llvm::Value* eq_i32 = builder_.CreateCall(str_eq, {a, b}, "streq");
        llvm::Value* is_eq =
            builder_.CreateICmpNE(eq_i32, llvm::ConstantInt::get(i32Ty(), 0), "streq.b");
        if (bin.op == "==") {
          return is_eq;
        }
        return builder_.CreateNot(is_eq, "strne");
      }

      // Interface / class-pointer equality: compare the raw pointer(s) instead of
      // falling through to `toI32`, which only handles integers. A fat pointer
      // compares equal when BOTH the data pointer and the vtable match.
      if ((bin.op == "==" || bin.op == "!=") && lhs && rhs &&
          lhs->getType()->isStructTy()) {
        llvm::StructType* st = llvm::cast<llvm::StructType>(lhs->getType());
        if (st == fatPtrTy()) {
          llvm::Function* cur = builder_.GetInsertBlock()->getParent();
          auto fieldPtr = [&](llvm::Value* v, unsigned idx) -> llvm::Value* {
            llvm::AllocaInst* slot = createEntryAlloca(cur, st, "iface.eq.tmp");
            builder_.CreateStore(v, slot);
            return builder_.CreateStructGEP(st, slot, idx, "iface.eq.f");
          };
          llvm::Value* data_eq = builder_.CreateICmpEQ(
              builder_.CreateLoad(ptrTy(), fieldPtr(lhs, 0), "iface.eq.a0"),
              builder_.CreateLoad(ptrTy(), fieldPtr(rhs, 0), "iface.eq.b0"), "iface.eq.d");
          llvm::Value* vt_eq = builder_.CreateICmpEQ(
              builder_.CreateLoad(ptrTy(), fieldPtr(lhs, 1), "iface.eq.a1"),
              builder_.CreateLoad(ptrTy(), fieldPtr(rhs, 1), "iface.eq.b1"), "iface.eq.v");
          llvm::Value* both = builder_.CreateAnd(data_eq, vt_eq, "iface.eq.both");
          return bin.op == "==" ? both : builder_.CreateNot(both, "iface.ne");
        }
      }
      if ((bin.op == "==" || bin.op == "!=") && lhs && rhs &&
          lhs->getType() == ptrTy() && rhs->getType() == ptrTy()) {
        return bin.op == "==" ? builder_.CreateICmpEQ(lhs, rhs, "ptreq")
                              : builder_.CreateICmpNE(lhs, rhs, "ptrne");
      }

      // Class value equality (`Array<Variant>` element etc.). A struct has no
      // scalar lowering, so the comparison would reach `toI32` and ICE. Reference
      // identity is the honest semantics here: structs are passed by value, so we
      // compare the address of the *canonical* object when one is available.
      // Struct/pointer equality (e.g. `Array<Variant>` element vs a `Variant`
      // parameter). Structs have no scalar lowering, so these operands would
      // otherwise reach `toI32` and ICE. Compare structurally when both sides are
      // the same struct type, and fall back to pointer identity otherwise.
      if ((bin.op == "==" || bin.op == "!=") && lhs && rhs &&
          !lhs->getType()->isIntegerTy() && !lhs->getType()->isFloatingPointTy() &&
          !isNumericType(lt) && !isNumericType(rt) && lt != "String" && rt != "String" &&
          lt != "ptr" && rt != "ptr") {
        auto spill = [&](llvm::Value* v) -> llvm::AllocaInst* {
          llvm::Function* cur = builder_.GetInsertBlock()->getParent();
          llvm::AllocaInst* slot = createEntryAlloca(cur, v->getType(), "val.eq.tmp");
          builder_.CreateStore(v, slot);
          return slot;
        };
        auto asAddr = [&](llvm::Value* v) -> llvm::Value* {
          if (v->getType()->isPointerTy()) {
            return v;
          }
          return spill(v);
        };

        if (lhs->getType()->isStructTy() && rhs->getType()->isStructTy() &&
            lhs->getType() == rhs->getType()) {
          // Field-wise comparison: equal only when every element matches.
          auto* st = llvm::cast<llvm::StructType>(lhs->getType());
          llvm::AllocaInst* sa = spill(lhs);
          llvm::AllocaInst* sb = spill(rhs);
          llvm::Value* all_eq = llvm::ConstantInt::getTrue(builder_.getContext());
          for (unsigned i = 0; i < st->getNumElements(); ++i) {
            llvm::Type* ft = st->getElementType(i);
            llvm::Value* fa = builder_.CreateStructGEP(st, sa, i, "val.eq.fa");
            llvm::Value* fb = builder_.CreateStructGEP(st, sb, i, "val.eq.fb");
            llvm::Value* eq = nullptr;
            if (ft->isPointerTy()) {
              eq = builder_.CreateICmpEQ(builder_.CreateLoad(ft, fa, "val.eq.la"),
                                         builder_.CreateLoad(ft, fb, "val.eq.lb"),
                                         "val.eq.p");
            } else if (ft->isIntegerTy()) {
              eq = builder_.CreateICmpEQ(builder_.CreateLoad(ft, fa, "val.eq.la"),
                                         builder_.CreateLoad(ft, fb, "val.eq.lb"),
                                         "val.eq.i");
            } else if (ft->isFloatingPointTy()) {
              eq = builder_.CreateFCmpOEQ(builder_.CreateLoad(ft, fa, "val.eq.la"),
                                          builder_.CreateLoad(ft, fb, "val.eq.lb"),
                                          "val.eq.f");
            } else {
              // Nested aggregate: not comparable element-wise yet; treat as unequal.
              eq = llvm::ConstantInt::getFalse(builder_.getContext());
            }
            all_eq = builder_.CreateAnd(all_eq, eq, "val.eq.all");
          }
          return bin.op == "==" ? all_eq : builder_.CreateNot(all_eq, "val.ne.all");
        }

        llvm::Value* pa = asAddr(lhs);
        llvm::Value* pb = asAddr(rhs);
        if (pa && pb) {
          return bin.op == "==" ? builder_.CreateICmpEQ(pa, pb, "val.eqtmp")
                                : builder_.CreateICmpNE(pa, pb, "val.netmp");
        }
      }

      // Comparisons: promote numeric operands to a common type.
      if (isNumericType(lt) && isNumericType(rt)) {
        const std::string promoted = promoteNumericTypes(lt, rt);
        lhs = emitNumericCast(lhs, lt, promoted, bin.loc);
        rhs = emitNumericCast(rhs, rt, promoted, bin.loc);
        if (!lhs || !rhs) {
          return nullptr;
        }
        if (promoted == "f64") {
          if (bin.op == "==") return builder_.CreateFCmpOEQ(lhs, rhs, "feqtmp");
          if (bin.op == "!=") return builder_.CreateFCmpONE(lhs, rhs, "fnetmp");
          if (bin.op == "<") return builder_.CreateFCmpOLT(lhs, rhs, "flttmp");
          if (bin.op == ">") return builder_.CreateFCmpOGT(lhs, rhs, "fgttmp");
          if (bin.op == "<=") return builder_.CreateFCmpOLE(lhs, rhs, "fletmp");
          if (bin.op == ">=") return builder_.CreateFCmpOGE(lhs, rhs, "fgetmp");
        } else {
          if (bin.op == "==") return builder_.CreateICmpEQ(lhs, rhs, "eqtmp");
          if (bin.op == "!=") return builder_.CreateICmpNE(lhs, rhs, "netmp");
          if (bin.op == "<") return builder_.CreateICmpSLT(lhs, rhs, "lttmp");
          if (bin.op == ">") return builder_.CreateICmpSGT(lhs, rhs, "gttmp");
          if (bin.op == "<=") return builder_.CreateICmpSLE(lhs, rhs, "letmp");
          if (bin.op == ">=") return builder_.CreateICmpSGE(lhs, rhs, "getmp");
        }
      } else {
        lhs = toI32(lhs, bin.loc);
        rhs = toI32(rhs, bin.loc);
        if (bin.op == "==") return builder_.CreateICmpEQ(lhs, rhs, "eqtmp");
        if (bin.op == "!=") return builder_.CreateICmpNE(lhs, rhs, "netmp");
        if (bin.op == "<") return builder_.CreateICmpSLT(lhs, rhs, "lttmp");
        if (bin.op == ">") return builder_.CreateICmpSGT(lhs, rhs, "gttmp");
        if (bin.op == "<=") return builder_.CreateICmpSLE(lhs, rhs, "letmp");
        if (bin.op == ">=") return builder_.CreateICmpSGE(lhs, rhs, "getmp");
      }

      error(expr.loc, "unsupported binary operator: " + bin.op);
      return nullptr;
    }
    case ast::ExprKind::Call:
      return emitGenericOrPlainCall(static_cast<const ast::CallExpr&>(expr));
  }
  // [A007] no AST loc available — `emitExpr` only sees the erased `ast::Expr` base.
  error(expr.loc, "unsupported expression in codegen");
  return nullptr;
}

llvm::Value* Codegen::emitHeapExpr(const ast::HeapExpr& expr) {
  if (!expr.inner || expr.inner->kind() != ast::ExprKind::New) {
    error(expr.loc, "heap requires ClassName(...) construction");
    return nullptr;
  }
  const auto& neu = static_cast<const ast::NewExpr&>(*expr.inner);
  const ClassInfo* info = findClass(neu.class_name);
  if (!info) {
    error(expr.loc, "unknown class in heap expression: " + neu.class_name);
    return nullptr;
  }
  ensureDataLayout();
  llvm::Function* alloc = requireExtern("l_alloc", neu.loc);
  if (!alloc) {
    return nullptr;
  }
  const uint64_t bytes = module_->getDataLayout().getTypeAllocSize(info->type);
  llvm::Value* size = llvm::ConstantInt::get(i64Ty(), bytes);
  llvm::Value* raw = builder_.CreateCall(alloc, {size}, "heap.raw");
  if (!initClassAtPtr(raw, *info, neu)) {
    return nullptr;
  }
  return raw;
}

bool Codegen::resolveCallArgs(const ast::FuncDeclStmt& fn, const std::vector<ast::CallArg>& raw,
                              std::vector<const ast::Expr*>& ordered) {
  // Match positional args left-to-right, then bind keyword args by parameter name.
  // Result is always in formal parameter declaration order for CreateCall.
  ordered.assign(fn.params.size(), nullptr);
  size_t next_pos = 0;
  for (const ast::CallArg& a : raw) {
    if (!a.value) {
      error(fn.loc, "null argument calling " + fn.name);
      return false;
    }
    if (a.name.empty()) {
      if (next_pos >= fn.params.size()) {
        error(fn.loc, "too many arguments calling " + fn.name);
        return false;
      }
      if (ordered[next_pos] != nullptr) {
        error(fn.loc, "duplicate argument for parameter '" + fn.params[next_pos].name + "'");
        return false;
      }
      ordered[next_pos] = a.value.get();
      ++next_pos;
      continue;
    }
    size_t idx = static_cast<size_t>(-1);
    for (size_t i = 0; i < fn.params.size(); ++i) {
      if (fn.params[i].name == a.name) {
        idx = i;
        break;
      }
    }
    if (idx == static_cast<size_t>(-1)) {
      error(fn.loc, "unknown keyword argument '" + a.name + "' for " + fn.name);
      return false;
    }
    if (ordered[idx] != nullptr) {
      error(fn.loc, "duplicate argument for parameter '" + a.name + "'");
      return false;
    }
    ordered[idx] = a.value.get();
  }
  for (size_t i = 0; i < ordered.size(); ++i) {
    if (ordered[i] == nullptr) {
      error(fn.loc, "missing argument '" + fn.params[i].name + "' calling " + fn.name);
      return false;
    }
  }
  return true;
}

llvm::Value* Codegen::emitNamedFunctionPointer(const std::string& name,
                                               const ast::SourceLocation& loc) {
  // Local `fun(...)` variable first.
  auto lit = local_types_.find(name);
  if (lit != local_types_.end() && isFunTypeName(lit->second)) {
    auto vit = named_values_.find(name);
    if (vit == named_values_.end()) {
      error(loc, "unknown function variable '" + name + "'");
      return nullptr;
    }
    return builder_.CreateLoad(ptrTy(), vit->second, name + ".fptr");
  }

  std::string ir_name = resolveFuncIrName(name);
  if (ir_name.empty()) {
    ir_name = name;
  }
  llvm::Function* fn = module_->getFunction(ir_name);
  if (!fn) {
    error(loc, "unknown function '" + name + "' (cannot take address for callback)");
    return nullptr;
  }
  fn->setCallingConv(llvm::CallingConv::C);
  return fn;  // Function* decays to ptr in LLVM
}

// Implicit auto-boxing: build `Variant.from_<kind>(value)` for a primitive or
// String argument. Returns nullptr when the value is already a Variant / class
// object or when no matching factory exists (caller then takes the normal path).
llvm::Value* Codegen::emitImplicitVariantBox(const ast::Expr& expr, llvm::Type* expected) {
  // Already a Variant: pass through untouched.
  // Only `exprTypeOf` is trustworthy here: `inferTypeFromExpr` falls back to
  // `i32` for unresolved MethodCalls, which would mis-box a Variant value.
  std::string src_ty = exprTypeOf(expr);
  if (expr.kind() == ast::ExprKind::StringLiteral) {
    src_ty = "String";
  } else if (src_ty.empty() && (expr.kind() == ast::ExprKind::FloatLiteral ||
                                expr.kind() == ast::ExprKind::IntLiteral ||
                                expr.kind() == ast::ExprKind::BoolLiteral)) {
    src_ty = inferTypeFromExpr(expr);
  }
  if (resolveTypeName(src_ty) == "Variant") {
    return nullptr;
  }
  // The callee already expects a Variant by value (`fun(Variant)`): the argument
  // is a Variant-producing expression, so no boxing applies.
  if (expected) {
    const ClassInfo* vinfo = findClass("Variant");
    if (vinfo && vinfo->type && expected == vinfo->type) {
      return nullptr;
    }
  }
  // Map source type → factory suffix declared in the Variant class.
  std::string suffix;
  if (src_ty == "String") {
    suffix = "string";
  } else if (src_ty == "i32" || src_ty == "i64") {
    suffix = "i32";
  } else if (src_ty == "f64") {
    suffix = "f64";
  } else if (src_ty == "bool") {
    suffix = "bool";
  } else {
    return nullptr;
  }
  const std::string factory = "from_" + suffix;
  auto fit = concrete_funcs_.find(factory);
  if (fit == concrete_funcs_.end() || !fit->second) {
    return nullptr;  // Variant stdlib not loaded — let the normal path diagnose.
  }
  llvm::Function* fn = module_->getFunction(resolveFuncIrName(factory));
  if (!fn) {
    return nullptr;
  }
  llvm::Value* arg = nullptr;
  // String payloads must arrive as String* (class by-reference ABI), not raw C chars.
  if (suffix == "string") {
    arg = emitValueForParam(expr, fn->getFunctionType()->getParamType(0), "String");
  } else {
    arg = emitExpr(expr);
  }
  if (!arg) {
    return nullptr;
  }
  llvm::Type* want = fn->getFunctionType()->getParamType(0);
  if (want->isIntegerTy(32) && !arg->getType()->isPointerTy()) {
    arg = toI32(arg, expr.loc);
  }
  if (arg->getType() != want) {
    return nullptr;  // Representation mismatch: fall back to the explicit path.
  }
  llvm::Value* boxed = builder_.CreateCall(fn, {arg}, "variant.box");
  // Class formals take a pointer (by-reference ABI): spill so the caller can GEP.
  if (expected && expected->isPointerTy() && !boxed->getType()->isPointerTy()) {
    llvm::Function* cur = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* tmp = createEntryAlloca(cur, boxed->getType(), "variant.box.tmp");
    builder_.CreateStore(boxed, tmp);
    return tmp;
  }
  return boxed;
}

llvm::Value* Codegen::emitValueForParam(const ast::Expr& expr, llvm::Type* expected,
                                        const std::string& expected_type_name) {
  // Class formals are ABI pointers: coerce string literals → temporary String*.
  if (expected_type_name == "String" && expr.kind() == ast::ExprKind::StringLiteral) {
    return emitStringObjectPtr(expr);
  }

  // Implicit auto-boxing: a primitive/String literal or value passed to a `Variant`
  // formal is lowered to the corresponding `Variant.from_*` factory. The mapping is
  // discovered from the declared factories, never hardcoded in codegen.
  // Only a bare `Variant` formal qualifies: `fun(Variant)` / `Array<Variant>` have
  // different representations and must keep their normal path.
  if (expected_type_name == "Variant") {
    llvm::Value* boxed = emitImplicitVariantBox(expr, expected);
    if (boxed) {
      return boxed;
    }
  }
  // Trailing block / lambda / bare function name → function pointer matching fun(...).
  if (!expected_type_name.empty() && isFunTypeName(expected_type_name)) {
    if (expr.kind() == ast::ExprKind::Lambda) {
      const auto& lam = static_cast<const ast::LambdaExpr&>(expr);
      // [dsl] Placeholder for a bare-brace config block: the real binding
      // happens at the owning call site (see callArgsForConfigBlock).
      if (lam.is_config_dsl) {
        pending_config_block_ = &lam;
        pending_config_expected_ = expected_type_name;
        return llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy()));
      }
      pending_closure_ctx_ = nullptr;
      pending_closure_ctx_ty_ = nullptr;
      llvm::Value* fn =
          emitLambda(static_cast<const ast::LambdaExpr&>(expr), expected_type_name);
      if (!fn) {
        return nullptr;
      }
      if (pending_closure_ctx_) {
        error(expr.loc,
              "capturing lambda cannot be passed as a bare fun(...) value (Phase 2 local-only)");
        pending_closure_ctx_ = nullptr;
        pending_closure_ctx_ty_ = nullptr;
        return nullptr;
      }
      return fn;
    }
    if (expr.kind() == ast::ExprKind::Identifier) {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      return emitNamedFunctionPointer(id.name, expr.loc);
    }
    llvm::Value* v = emitExpr(expr);
    if (!v) {
      return nullptr;
    }
    return v;
  }

  // `ptr` formals can also receive a bare L function (C-style callback as opaque ptr).
  if (expected_type_name == "ptr" && expr.kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
    auto lit = local_types_.find(id.name);
    if (lit == local_types_.end() || !isFunTypeName(lit->second)) {
      // Prefer function symbol over treating as a variable load.
      if (resolveFuncIrName(id.name).size() != 0 || module_->getFunction(id.name)) {
        return emitNamedFunctionPointer(id.name, expr.loc);
      }
    }
  }

  // [link:50-Variant] Contextual unbox: a `Variant` argument passed to a String /
  // primitive formal is lowered to `arg.as_<t>()` (same rule as `return`).
  if (!expected_type_name.empty() && expected_type_name != "Variant") {
    llvm::Value* unboxed =
        emitContextualVariantUnbox(expr, expected_type_name, expected, /*immediate=*/true);
    if (unboxed) {
      return unboxed;
    }
  }

  if (!expected_type_name.empty() && isClassType(expected_type_name)) {
    // `classValuePtr` already knows how to reach a captured / heap-local object;
    // going through emitExpr here would load the struct by value instead.
    if (expr.kind() == ast::ExprKind::Identifier) {
      llvm::Value* obj = classValuePtr(expr, expected_type_name);
      if (!obj) {
        return nullptr;
      }
      return obj;
    }
    llvm::Value* v = emitExpr(expr);
    if (!v) {
      return nullptr;
    }
    if (v->getType()->isPointerTy()) {
      return v;
    }
    // Rare: class value in a register — spill so we can pass Class*.
    ensureTypeAvailable(expected_type_name);
    const ClassInfo* info = findClass(expected_type_name);
    if (!info || v->getType() != info->type) {
      error(expr.loc, "class argument type mismatch for " + expected_type_name);
      return nullptr;
    }
    llvm::Function* cur = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, "arg.class");
    builder_.CreateStore(v, tmp);
    return tmp;
  }

  // Interface formal: auto-box Class → { data_ptr, vtable_ptr } fat pointer.
  if (!expected_type_name.empty() && isInterfaceType(expected_type_name)) {
    if (expr.kind() == ast::ExprKind::Identifier) {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      auto lit = local_types_.find(id.name);
      if (lit != local_types_.end() && isClassType(lit->second)) {
        llvm::Value* obj = classObjectPtr(id.name, expr.loc);
        if (!obj) {
          return nullptr;
        }
        return boxToInterface(obj, lit->second, expected_type_name, expr.loc);
      }
      if (lit != local_types_.end() && isInterfaceType(lit->second)) {
        return emitExpr(expr);  // already a fat pointer value
      }
    }
    if (expr.kind() == ast::ExprKind::New) {
      const auto& neu = static_cast<const ast::NewExpr&>(expr);
      // Heap-escape: Array<IFace>.push(Class(...)) stores the fat pointer beyond this call.
      llvm::Value* obj = heapAllocNewExpr(neu);
      if (!obj) {
        return nullptr;
      }
      return boxToInterface(obj, neu.class_name, expected_type_name, neu.loc);
    }
    llvm::Value* v = emitExpr(expr);
    if (!v) {
      return nullptr;
    }
    if (v->getType() == fatPtrTy()) {
      return v;
    }
    if (v->getType()->isPointerTy() && expected->isStructTy()) {
      // Spill slot holding a fat pointer.
      return builder_.CreateLoad(expected, v, "arg.iface");
    }
    error(expr.loc, "cannot pass value as interface '" + expected_type_name + "'");
    return nullptr;
  }

  llvm::Value* v = emitExpr(expr);
  if (!v) {
    return nullptr;
  }
  if (expected->isStructTy()) {
    // Remaining by-value aggregates.
    if (v->getType()->isPointerTy()) {
      v = builder_.CreateLoad(expected, v, "arg.struct");
    }
  } else if (expected->isIntegerTy(1)) {
    v = toI1(v);
  } else if (expected->isIntegerTy(64) && v->getType()->isIntegerTy()) {
    v = builder_.CreateIntCast(v, i64Ty(), /*isSigned=*/true, "arg.i64");
  } else if (expected->isIntegerTy(32) && !v->getType()->isPointerTy()) {
    v = toI32(v, expr.loc);
  }
  return v;
}

llvm::Value* Codegen::emitGenericOrPlainCall(const ast::CallExpr& call) {
  auto requirePositional = [&](size_t n, const char* name) -> bool {
    if (call.args.size() != n) {
      error(call.loc, std::string(name) + " expects exactly " + std::to_string(n) + " argument(s)");
      return false;
    }
    for (const ast::CallArg& a : call.args) {
      if (!a.name.empty()) {
        error(call.loc, std::string(name) + " does not accept keyword arguments");
        return false;
      }
      if (!a.value) {
        error(call.loc, "null argument to " + std::string(name));
        return false;
      }
    }
    return true;
  };

  // [link:93-Compile-Time-Unique-IDs]: never emit a runtime call for uid_counter (semantic should have rejected).
  if (call.callee == "uid_counter") {
    error(call.loc, "uid_counter() is only allowed in compile-time context");
    return nullptr;
  }
  if (call.callee == "compile_error" || call.callee == "comptime_assert") {
    error(call.loc, call.callee + "() is only allowed in compile-time context");
    return nullptr;
  }

  // [link:31-Auto-Free-Injection] `l_free(x)` taken on a compiler-tracked owning local hands the
  // release over to the program: drop `x` from every auto-free scope so the
  // end-of-scope injection does not free the same pointer a second time.
  // A `defer l_free(x)` is already handled by noteExplicitLFreeInDefer.
  if (call.callee == "l_free" && call.args.size() == 1 && call.args[0].value &&
      call.args[0].value->kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(*call.args[0].value);
    cancelAutoFreeLocal(id.name);
  }

  // [dsl] `app { ... }` / `app(type: App) { ... }` — not a real call: the
  // callee owns a config root (default `CPropertys`) and the block runs inline.
  for (const ast::CallArg& a : call.args) {
    if (a.value && a.value->kind() == ast::ExprKind::Lambda &&
        static_cast<const ast::LambdaExpr&>(*a.value).is_config_dsl) {
      return emitConfigBlockCall(call, static_cast<const ast::LambdaExpr&>(*a.value));
    }
  }

  // [link:42-Closure-Integration-Data-Race-Refinement] `delay(ms) { ... }` /
  // `delay(ms, || ...)` with inline Lambda -> spawn desugar.
  if (call.callee == "delay" && call.args.size() == 2 && call.args[0].value &&
      call.args[1].value && call.args[1].value->kind() == ast::ExprKind::Lambda) {
    return emitDelayInline(*call.args[0].value,
                           static_cast<const ast::LambdaExpr&>(*call.args[1].value), call.loc);
  }

  // Capturing closure local: `add_base(5)` → call __lambda(ctx, 5).
  {
    auto cit = closure_locals_.find(call.callee);
    if (cit != closure_locals_.end() && cit->second.fn && cit->second.ctx) {
      llvm::Function* cfn = cit->second.fn;
      llvm::FunctionType* fty = cfn->getFunctionType();
      if (fty->getNumParams() != call.args.size() + 1) {
        error(call.loc, "argument count mismatch calling closure '" + call.callee + "'");
        return nullptr;
      }
      auto lit = local_types_.find(call.callee);
      std::vector<std::string> param_tys;
      std::string ret;
      bool has_ret = false;
      if (lit != local_types_.end()) {
        parseFunTypeName(resolveTypeName(lit->second), param_tys, ret, has_ret);
      }
      std::vector<llvm::Value*> args;
      args.push_back(cit->second.ctx);
      for (size_t i = 0; i < call.args.size(); ++i) {
        if (!call.args[i].value) {
          error(call.loc, "null argument to closure '" + call.callee + "'");
          return nullptr;
        }
        if (!call.args[i].name.empty()) {
          error(call.loc, "keyword arguments not supported on closures");
          return nullptr;
        }
        llvm::Type* expected = fty->getParamType(static_cast<unsigned>(i + 1));
        std::string expected_name = i < param_tys.size() ? param_tys[i] : std::string();
        llvm::Value* v = emitValueForParam(*call.args[i].value, expected, expected_name);
        if (!v) {
          return nullptr;
        }
        args.push_back(v);
      }
      if (fty->getReturnType()->isVoidTy()) {
        builder_.CreateCall(cfn, args);
        return llvm::ConstantInt::get(i32Ty(), 0);
      }
      return builder_.CreateCall(cfn, args, "closure.call");
    }
  }

  // Escaped fat closure local: `cb()` → extract {fn,ctx}, call fn(ctx, ...).
  if (fat_closure_locals_.count(call.callee) != 0) {
    auto lit = local_types_.find(call.callee);
    auto vit = named_values_.find(call.callee);
    if (lit == local_types_.end() || vit == named_values_.end()) {
      error(call.loc, "fat closure '" + call.callee + "' missing local state");
      return nullptr;
    }
    std::vector<std::string> param_tys;
    std::string ret;
    bool has_ret = false;
    if (!parseFunTypeName(resolveTypeName(lit->second), param_tys, ret, has_ret)) {
      error(call.loc, "invalid fat-closure payload type for '" + call.callee + "'");
      return nullptr;
    }
    if (param_tys.size() != call.args.size()) {
      error(call.loc, "argument count mismatch calling fat closure '" + call.callee + "'");
      return nullptr;
    }
    llvm::Type* rty = (!has_ret || ret == "void") ? voidTy() : llvmTypeForTypeName(ret);
    std::vector<llvm::Type*> pts;
    pts.push_back(ptrTy());  // ctx*
    for (const std::string& p : param_tys) {
      pts.push_back(abiTypeForParam(p));
    }
    llvm::FunctionType* fty = llvm::FunctionType::get(rty, pts, /*isVarArg=*/false);

    llvm::Value* fat = builder_.CreateLoad(fatClosureTy(), vit->second, call.callee + ".fat");
    llvm::Value* fn_ptr = builder_.CreateExtractValue(fat, 0, call.callee + ".fn");
    llvm::Value* ctx = builder_.CreateExtractValue(fat, 1, call.callee + ".ctx");
    std::vector<llvm::Value*> args;
    args.push_back(ctx);
    for (size_t i = 0; i < call.args.size(); ++i) {
      if (!call.args[i].value) {
        error(call.loc, "null argument to fat closure '" + call.callee + "'");
        return nullptr;
      }
      if (!call.args[i].name.empty()) {
        error(call.loc, "keyword arguments not supported on fat closures");
        return nullptr;
      }
      llvm::Type* expected = fty->getParamType(static_cast<unsigned>(i + 1));
      llvm::Value* v = emitValueForParam(*call.args[i].value, expected, param_tys[i]);
      if (!v) {
        return nullptr;
      }
      args.push_back(v);
    }
    if (fty->getReturnType()->isVoidTy()) {
      builder_.CreateCall(fty, fn_ptr, args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(fty, fn_ptr, args, "fat.closure.call");
  }

  // Spawn/lambda capture: `callback()` when `callback` is a captured `fun(...)` local.
  {
    auto cap = capture_slots_.find(call.callee);
    auto ty_it = capture_types_.find(call.callee);
    if (cap != capture_slots_.end() && current_lambda_ctx_ && current_lambda_ctx_ty_ &&
        ty_it != capture_types_.end() && isFunTypeName(ty_it->second)) {
      llvm::FunctionType* fty = llvmFunctionTypeForFunName(ty_it->second, call.loc);
      if (!fty) {
        return nullptr;
      }
      if (fty->getNumParams() != call.args.size()) {
        error(call.loc, "argument count mismatch calling captured function '" + call.callee + "'");
        return nullptr;
      }
      llvm::Value* slot = builder_.CreateStructGEP(current_lambda_ctx_ty_, current_lambda_ctx_,
                                                   cap->second, call.callee + ".cap");
      llvm::Value* fn_ptr = nullptr;
      if (current_lambda_ctx_by_value_) {
        fn_ptr = builder_.CreateLoad(ptrTy(), slot, call.callee + ".fn");
      } else {
        llvm::Value* local_ptr =
            builder_.CreateLoad(ptrTy(), slot, call.callee + ".ptr");
        fn_ptr = builder_.CreateLoad(ptrTy(), local_ptr, call.callee + ".fn");
      }
      std::vector<std::string> param_tys;
      std::string ret;
      bool has_ret = false;
      parseFunTypeName(resolveTypeName(ty_it->second), param_tys, ret, has_ret);
      std::vector<llvm::Value*> args;
      for (size_t i = 0; i < call.args.size(); ++i) {
        if (!call.args[i].value) {
          error(call.loc, "null argument to captured function '" + call.callee + "'");
          return nullptr;
        }
        if (!call.args[i].name.empty()) {
          error(call.loc, "keyword arguments not supported on captured functions");
          return nullptr;
        }
        llvm::Type* expected = fty->getParamType(static_cast<unsigned>(i));
        std::string expected_name = i < param_tys.size() ? param_tys[i] : std::string();
        llvm::Value* v = emitValueForParam(*call.args[i].value, expected, expected_name);
        if (!v) {
          return nullptr;
        }
        args.push_back(v);
      }
      if (fty->getReturnType()->isVoidTy()) {
        builder_.CreateCall(fty, fn_ptr, args);
        return llvm::ConstantInt::get(i32Ty(), 0);
      }
      return builder_.CreateCall(fty, fn_ptr, args, "cap.fptr.call");
    }
  }

  // First-class function local: `action(x)` where action : fun(T) (no captures).
  {
    auto lit = local_types_.find(call.callee);
    auto vit = named_values_.find(call.callee);
    if (lit != local_types_.end() && vit != named_values_.end() && isFunTypeName(lit->second)) {
      llvm::FunctionType* fty = llvmFunctionTypeForFunName(lit->second, call.loc);
      if (!fty) {
        return nullptr;
      }
      if (fty->getNumParams() != call.args.size()) {
        error(call.loc, "argument count mismatch calling function value '" + call.callee + "'");
        return nullptr;
      }
      llvm::Value* fn_ptr = builder_.CreateLoad(ptrTy(), vit->second, call.callee);
      std::vector<std::string> param_tys;
      std::string ret;
      bool has_ret = false;
      parseFunTypeName(resolveTypeName(lit->second), param_tys, ret, has_ret);
      std::vector<llvm::Value*> args;
      for (size_t i = 0; i < call.args.size(); ++i) {
        if (!call.args[i].value) {
          error(call.loc, "null argument to function value '" + call.callee + "'");
          return nullptr;
        }
        if (!call.args[i].name.empty()) {
          error(call.loc, "keyword arguments not supported on function values");
          return nullptr;
        }
        llvm::Type* expected = fty->getParamType(static_cast<unsigned>(i));
        std::string expected_name = i < param_tys.size() ? param_tys[i] : std::string();
        llvm::Value* v = emitValueForParam(*call.args[i].value, expected, expected_name);
        if (!v) {
          return nullptr;
        }
        args.push_back(v);
      }
      if (fty->getReturnType()->isVoidTy()) {
        builder_.CreateCall(fty, fn_ptr, args);
        return llvm::ConstantInt::get(i32Ty(), 0);
      }
      return builder_.CreateCall(fty, fn_ptr, args, "fptr.call");
    }
  }

  // Inside a method body: bare sibling call `grow()` / `insert_raw(...)` → this->method.
  if (!current_class_.empty() && current_this_) {
    const ClassInfo* info = findClass(current_class_);
    if (info) {
      auto mit = info->methods.find(call.callee);
      if (mit != info->methods.end()) {
        const ast::FuncDeclStmt* method_ast = nullptr;
        if (info->ast) {
          for (const auto& m : info->ast->methods) {
            if (m && m->name == call.callee) {
              method_ast = m.get();
              break;
            }
          }
        }
        std::vector<const ast::Expr*> ordered;
        if (method_ast) {
          if (!resolveCallArgs(*method_ast, call.args, ordered)) {
            return nullptr;
          }
        } else {
          for (const ast::CallArg& a : call.args) {
            if (!a.name.empty()) {
              error(call.loc, "keyword arguments require a known method signature");
              return nullptr;
            }
            if (!a.value) {
              error(call.loc, "null argument in sibling method call");
              return nullptr;
            }
            ordered.push_back(a.value.get());
          }
        }
        std::vector<llvm::Value*> call_args;
        call_args.push_back(current_this_);
        unsigned ai = 1;
        for (const ast::Expr* arg : ordered) {
          llvm::Type* expected = mit->second->getFunctionType()->getParamType(ai);
          std::string expected_name;
          if (method_ast && ai - 1 < method_ast->params.size()) {
            expected_name = resolveTypeName(method_ast->params[ai - 1].type_name);
          }
          ++ai;
          llvm::Value* v = emitValueForParam(*arg, expected, expected_name);
          if (!v) {
            return nullptr;
          }
          call_args.push_back(v);
        }
        if (mit->second->arg_size() != call_args.size()) {
          error(call.loc, "argument count mismatch calling " + current_class_ + "." + call.callee);
          return nullptr;
        }
        if (mit->second->getReturnType()->isVoidTy()) {
          builder_.CreateCall(mit->second, call_args);
          return llvm::ConstantInt::get(i32Ty(), 0);
        }
        return builder_.CreateCall(mit->second, call_args, "selfcall");
      }
    }
  }

  if (call.callee == "arena") {
    return emitArenaBuiltin(call);
  }

  if (call.callee == "print") {
    // Language builtin: lowers to extern l_print_* from stdlib/io.l.
    if (!requirePositional(1, "print")) {
      return nullptr;
    }
    const ast::Expr& arg_expr = *call.args[0].value;
    if (isStringOperand(arg_expr)) {
      llvm::Function* print_str = requireExtern("l_print_str", call.loc);
      if (!print_str) {
        return nullptr;
      }
      if (arg_expr.kind() == ast::ExprKind::StringLiteral) {
        llvm::Value* data = emitExpr(arg_expr);
        if (!data) {
          return nullptr;
        }
        builder_.CreateCall(print_str, {data});
      } else {
        llvm::Value* base = emitStringObjectPtr(arg_expr);
        if (!base) {
          return nullptr;
        }
        ensureTypeAvailable("String");
        const ClassInfo* info = findClass("String");
        if (!info) {
          error(call.loc, "String type not available for print");
          return nullptr;
        }
        auto fit = info->field_index.find("data_");
        if (fit == info->field_index.end()) {
          error(call.loc, "String.data_ not found");
          return nullptr;
        }
        llvm::Value* gep =
            builder_.CreateStructGEP(info->type, base, fit->second, "print.str.data");
        llvm::Value* data = builder_.CreateLoad(ptrTy(), gep, "print.str.ptr");
        builder_.CreateCall(print_str, {data});
      }
      llvm::Value* nl = builder_.CreateGlobalString("\n", "nl");
      builder_.CreateCall(print_str, {nl});
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    const std::string arg_ty = exprTypeOf(arg_expr);
    const std::string resolved_arg_ty = resolveTypeName(arg_ty);
    // [link:50-Variant] `print(v)`: v:Variant prints its payload by tag; the Optional
    // form prints the payload when present and a blank line when absent.
    if (resolved_arg_ty == "Variant" || resolved_arg_ty == "Optional<Variant>") {
      llvm::Value* v = emitExpr(arg_expr);
      if (!v) {
        return nullptr;
      }
      if (resolved_arg_ty == "Variant") {
        return emitPrintVariant(arg_expr, v, call.loc);
      }
      return emitPrintOptionalVariant(arg_expr, v, call.loc);
    }
    llvm::Value* arg = emitExpr(arg_expr);
    if (!arg) {
      return nullptr;
    }
    // Dispatch by L type / LLVM type → matching l_print_* C ABI helper.
    if (arg_ty == "bool" || arg->getType()->isIntegerTy(1)) {
      llvm::Function* print_b = requireExtern("l_print_bool", call.loc);
      if (!print_b) {
        return nullptr;
      }
      llvm::Value* as_i32 = builder_.CreateZExt(toI1(arg), i32Ty(), "print.bool");
      builder_.CreateCall(print_b, {as_i32});
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    if (arg_ty == "f64" || arg->getType()->isDoubleTy()) {
      llvm::Function* print_f = requireExtern("l_print_f64", call.loc);
      if (!print_f) {
        return nullptr;
      }
      if (!arg->getType()->isDoubleTy()) {
        arg = emitNumericCast(arg, arg_ty.empty() ? "i32" : arg_ty, "f64", call.loc);
        if (!arg) {
          return nullptr;
        }
      }
      builder_.CreateCall(print_f, {arg});
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    if (arg_ty == "i64" || arg->getType()->isIntegerTy(64)) {
      llvm::Function* print_i64 = requireExtern("l_print_i64", call.loc);
      if (!print_i64) {
        return nullptr;
      }
      if (!arg->getType()->isIntegerTy(64)) {
        arg = emitNumericCast(arg, arg_ty.empty() ? "i32" : arg_ty, "i64", call.loc);
        if (!arg) {
          return nullptr;
        }
      }
      builder_.CreateCall(print_i64, {arg});
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    llvm::Function* print_int = requireExtern("l_print_int", call.loc);
    if (!print_int) {
      return nullptr;
    }
    arg = toI32(arg, call.loc);
    if (!arg) {
      return nullptr;
    }
    builder_.CreateCall(print_int, {arg});
    return llvm::ConstantInt::get(i32Ty(), 0);
  }

  // Generic monomorphization: print_val(p) -> print_val_Point
  auto git = generic_templates_.find(call.callee);
  if (git == generic_templates_.end() && !current_module_.empty()) {
    git = generic_templates_.find(current_module_ + "::" + call.callee);
  }
  if (git != generic_templates_.end()) {
    const ast::FuncDeclStmt& tmpl = *git->second;
    std::vector<const ast::Expr*> ordered;
    if (!resolveCallArgs(tmpl, call.args, ordered)) {
      return nullptr;
    }
    std::vector<std::string> arg_types;
    std::vector<llvm::Value*> call_args;
    for (size_t i = 0; i < ordered.size(); ++i) {
      const ast::Expr* arg = ordered[i];
      std::string ty_name;
      if (arg->kind() == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*arg);
        auto lit = local_types_.find(id.name);
        if (lit != local_types_.end()) {
          ty_name = lit->second;
        }
      }
      if (ty_name.empty()) {
        ty_name = inferTypeFromExpr(*arg);
      }
      if (ty_name.empty()) {
        error(call.loc, "cannot infer type for argument " + std::to_string(i) + " of " +
                            call.callee);
        return nullptr;
      }
      arg_types.push_back(ty_name);

      if (isClassType(ty_name)) {
        llvm::Value* obj = nullptr;
        if (arg->kind() == ast::ExprKind::Identifier) {
          const auto& id = static_cast<const ast::IdentifierExpr&>(*arg);
          obj = classObjectPtr(id.name, id.loc);
        } else {
          // Temporaries like `Good()` — NewExpr yields a Class* alloca.
          obj = emitExpr(*arg);
        }
        if (!obj) {
          return nullptr;
        }
        call_args.push_back(obj);
      } else {
        llvm::Value* v = emitExpr(*arg);
        if (!v) {
          return nullptr;
        }
        call_args.push_back(toI32(v, arg->loc));
      }
    }

    llvm::Function* specialized = ensureSpecialized(tmpl, arg_types, call.loc);
    if (!specialized) {
      return nullptr;
    }
    if (specialized->getReturnType()->isVoidTy()) {
      builder_.CreateCall(specialized, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(specialized, call_args, "gcall");
  }

  // Concrete function: reorder KW args via AST signature, then CreateCall.
  const ast::FuncDeclStmt* fn_ast = nullptr;
  auto fit = concrete_funcs_.find(call.callee);
  if (fit == concrete_funcs_.end() && !current_module_.empty()) {
    fit = concrete_funcs_.find(current_module_ + "::" + call.callee);
  }
  if (fit != concrete_funcs_.end()) {
    fn_ast = fit->second;
  }

  const std::string ir_name = resolveFuncIrName(call.callee);
  if (ir_name.empty()) {
    error(call.loc, "unknown function: " + call.callee);
    return nullptr;
  }
  llvm::Function* callee = module_->getFunction(ir_name);
  if (!callee) {
    error(call.loc, "unknown function: " + call.callee);
    return nullptr;
  }

  std::vector<const ast::Expr*> ordered;
  if (fn_ast) {
    if (!resolveCallArgs(*fn_ast, call.args, ordered)) {
      return nullptr;
    }
  } else {
    if (callee->arg_size() != call.args.size()) {
      error(call.loc, "argument count mismatch calling " + call.callee);
      return nullptr;
    }
    for (const ast::CallArg& a : call.args) {
      if (!a.name.empty()) {
        error(call.loc, "keyword arguments require a known function signature for " + call.callee);
        return nullptr;
      }
      if (!a.value) {
        error(call.loc, "null argument calling " + call.callee);
        return nullptr;
      }
      ordered.push_back(a.value.get());
    }
  }

  if (callee->arg_size() != ordered.size()) {
    error(call.loc, "argument count mismatch calling " + call.callee);
    return nullptr;
  }
  std::vector<llvm::Value*> args;
  for (size_t i = 0; i < ordered.size(); ++i) {
    llvm::Type* expected = callee->getFunctionType()->getParamType(static_cast<unsigned>(i));
    std::string expected_name;
    if (fn_ast) {
      expected_name = fn_ast->params[i].type_name;
    }
    llvm::Value* v = emitValueForParam(*ordered[i], expected, expected_name);
    if (!v) {
      return nullptr;
    }
    args.push_back(v);
  }
  // [link:109-Performance-First-Architecture] Inline hot string primitives straight to IR instead of emitting a
  // CallInst. Only the instruction stream changes; the runtime symbols and the
  // language semantics are untouched.
  if (llvm::Value* inlined = tryEmitInlinedStringBuiltin(call, args)) {
    return inlined;
  }

  if (callee->getReturnType()->isVoidTy()) {
    builder_.CreateCall(callee, args);
    return llvm::ConstantInt::get(i32Ty(), 0);
  }
  return builder_.CreateCall(callee, args, "calltmp");
}

// `mod::fn(args)` — resolve the callee inside the named module. `pub` symbols
// keep an unprefixed IR name (see mangleName), so this is just the plain call
// path with an explicit module-qualified signature lookup.
// Converts a String-typed operand to its raw C string pointer. `Array<String>`
// elements travel by VALUE (`names.get(i)`), so a stack home is created first
// when the operand is not already a pointer to the String object.
llvm::Value* Codegen::stringOperandToCStr(llvm::Value* v, const std::string& ty,
                                          const ast::Expr& e) {
  const std::string eff_ty = ty.empty() ? exprTypeOf(e) : ty;
  if (eff_ty != "String") {
    return v;
  }
  if (!v) {
    return nullptr;
  }
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    return nullptr;
  }
  auto fit = info->field_index.find("data_");
  if (fit == info->field_index.end()) {
    return nullptr;
  }

  llvm::Value* base = v;
  if (v->getType() != ptrTy()) {
    if (v->getType() != info->type) {
      return nullptr;
    }
    llvm::Function* cur = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* slot = createEntryAlloca(cur, info->type, "streq.tmp");
    builder_.CreateStore(v, slot);
    base = slot;
  }
  llvm::Value* gep = builder_.CreateStructGEP(info->type, base, fit->second, "streq.data");
  return builder_.CreateLoad(ptrTy(), gep, "streq.cstr");
}

// Address of one field slot on `receiver`, following a pointer-typed field once.
// `field_ty` is the stored LLVM type: it is needed because `CreateLoad` bitcasts
// the pointer away from the identified struct type in debug (`-g`) builds.
llvm::Value* Codegen::fieldSlotAddress(const ClassInfo& owner, llvm::Value* receiver,
                                       size_t field_idx, const std::string& name,
                                       llvm::Type* field_ty) {
  llvm::Value* slot = builder_.CreateStructGEP(owner.type, receiver, field_idx, name + ".slot");
  if (!field_ty->isPointerTy()) {
    return slot;  // inline struct field: the GEP already addresses it
  }
  return builder_.CreateLoad(field_ty, slot, name + ".self");
}

// Loads a field of the current class through `this` ([link:34-Property-Fallback-Rule] fallback).
llvm::Value* Codegen::loadFieldOnThis(const std::string& field_name) {
  const ClassInfo* info = findClass(current_class_);
  if (!info) {
    return nullptr;
  }
  auto fit = info->field_index.find(physicalFieldName(field_name));
  if (fit == info->field_index.end()) {
    return nullptr;
  }
  if (current_method_is_static_ || !current_this_) {
    error(lastTopLevelLoc(), "cannot access instance field '" + field_name + "' from a static method");
    // [A007] no AST loc available
    return nullptr;
  }
  llvm::Type* fty = info->type->getElementType(static_cast<unsigned>(fit->second));
  llvm::Value* gep =
      builder_.CreateStructGEP(info->type, current_this_, fit->second, field_name);
  return builder_.CreateLoad(fty, gep, field_name);
}

// `findMethodAst` returns the AST declaration backing the emitted function.
const ast::FuncDeclStmt* Codegen::findMethodAst(const ClassInfo& info,
                                                const std::string& name) const {
  if (!info.ast) {
    return nullptr;
  }
  for (const auto& m : info.ast->methods) {
    if (m && m->name == name) {
      return m.get();
    }
  }
  return nullptr;
}

// Builds the LLVM argument list for a method call, converting each argument to
// the parameter type. `first_param_consumed` is 1 when `self` was pushed first.
bool Codegen::emitMethodArgs(const ast::MethodCallExpr& call,
                             const ast::FuncDeclStmt* method_ast, const ClassInfo& callee_info,
                             llvm::Function* target, unsigned first_param_consumed,
                             std::vector<llvm::Value*>& out) {
  std::vector<const ast::Expr*> ordered;
  if (method_ast) {
    if (!resolveCallArgs(*method_ast, call.args, ordered)) {
      return false;
    }
  } else {
    for (const ast::CallArg& a : call.args) {
      if (!a.value) {
        return false;
      }
      ordered.push_back(a.value.get());
    }
  }

  const bool is_static = method_ast && method_ast->is_static;
  unsigned ai = first_param_consumed;
  for (const ast::Expr* arg : ordered) {
    llvm::Type* expected = target->getFunctionType()->getParamType(ai);
    std::string expected_name;
    const size_t param_i = is_static ? static_cast<size_t>(ai) : static_cast<size_t>(ai - 1);
    if (method_ast && param_i < method_ast->params.size()) {
      expected_name = instantiateMemberType(method_ast->params[param_i].type_name, callee_info);
    }
    llvm::Value* v = emitValueForParam(*arg, expected, expected_name);
    if (!v) {
      return false;
    }
    out.push_back(v);
    ++ai;
  }
  return true;
}

// Emits the call and returns its value (0 for void), or nullptr on error.
llvm::Value* Codegen::finishMethodCall(llvm::Function* target,
                                       std::vector<llvm::Value*>& args,
                                       const std::string& what,
                                       const ast::SourceLocation& loc) {
  if (target->arg_size() != args.size()) {
    error(loc, "argument count mismatch calling " + what);
    return nullptr;
  }
  if (target->getReturnType()->isVoidTy()) {
    builder_.CreateCall(target, args);
    return llvm::ConstantInt::get(i32Ty(), 0);
  }
  return builder_.CreateCall(target, args, "mcall");
}

// `this.field.method(...)` — the receiver lives in one of the enclosing class's
// fields rather than in a local slot.
llvm::Value* Codegen::emitMethodCallOnField(const ast::MethodCallExpr& call,
                                            const std::string& field_name) {
  const ClassInfo* owner = findClass(current_class_);
  if (!owner) {
    return nullptr;
  }
  auto fit = owner->field_index.find(physicalFieldName(field_name));
  if (fit == owner->field_index.end()) {
    return nullptr;
  }
  const size_t fi = fit->second;
  if (fi >= owner->field_types.size()) {
    return nullptr;
  }
  const std::string field_ty = resolveTypeName(owner->field_types[fi]);
  if (field_ty.empty() || !isClassType(field_ty)) {
    return nullptr;
  }
  const ClassInfo* callee_info = findClass(field_ty);
  if (!callee_info) {
    return nullptr;
  }
  auto own = callee_info->methods.find(call.method);
  if (own == callee_info->methods.end()) {
    return nullptr;
  }
  llvm::Function* target = own->second;

  llvm::Value* field_ptr = fieldSlotAddress(
      *owner, current_this_, fi, field_name,
      owner->type->getElementType(static_cast<unsigned>(fi)));
  const ast::FuncDeclStmt* method_ast = findMethodAst(*callee_info, call.method);
  const bool is_static = method_ast && method_ast->is_static;

  std::vector<llvm::Value*> call_args;
  unsigned consumed = 0;
  if (!is_static) {
    call_args.push_back(field_ptr);
    consumed = 1;
  }
  if (!emitMethodArgs(call, method_ast, *callee_info, target, consumed, call_args)) {
    return nullptr;
  }
  return finishMethodCall(target, call_args, field_ty + "." + call.method, call.loc);
}

// `a::b::fn(...)` — resolves the callee declaration from `scope`'s exports and
// issues the call. Shared by scoped calls and namespace-qualified member calls.
llvm::Value* Codegen::emitExportedCall(const std::string& scope, const std::vector<ast::CallArg>& raw_args,
                                       const std::string& display, const ast::SourceLocation& loc) {
  const std::string callee_name = display.substr(display.find("::") + 2);
  const ast::FuncDeclStmt* fn_ast = nullptr;
  auto mit = module_exports_.find(scope);
  if (mit != module_exports_.end()) {
    auto fit = mit->second.find(callee_name);
    if (fit != mit->second.end()) {
      fn_ast = fit->second;
    }
  }
  if (!fn_ast) {
    auto cit = concrete_funcs_.find(scope + "::" + callee_name);
    if (cit != concrete_funcs_.end()) {
      fn_ast = cit->second;
    }
  }
  if (!fn_ast) {
    error(loc, "'" + callee_name + "' is not a pub function of module '" + scope + "'");
    return nullptr;
  }

  const std::string ir_name = resolveFuncIrName(fn_ast->name);
  llvm::Function* callee = ir_name.empty() ? nullptr : module_->getFunction(ir_name);
  if (!callee) {
    error(loc, "unknown function: " + display);
    return nullptr;
  }

  std::vector<const ast::Expr*> ordered;
  if (!resolveCallArgs(*fn_ast, raw_args, ordered)) {
    return nullptr;
  }
  if (callee->arg_size() != ordered.size()) {
    error(loc, "argument count mismatch calling " + display);
    return nullptr;
  }
  std::vector<llvm::Value*> args;
  for (size_t i = 0; i < ordered.size(); ++i) {
    llvm::Type* expected = callee->getFunctionType()->getParamType(static_cast<unsigned>(i));
    llvm::Value* v = emitValueForParam(*ordered[i], expected, fn_ast->params[i].type_name);
    if (!v) {
      return nullptr;
    }
    args.push_back(v);
  }
  // Same ABI at every call site: heap classes come back as pointers, by-value
  // classes (String) are spilled so callers always see a class pointer.
  const bool struct_ret = callee->getReturnType()->isStructTy();
  llvm::Value* call_val =
      builder_.CreateCall(callee, args, struct_ret ? display + ".ret" : "scoped");
  if (callee->getReturnType()->isVoidTy()) {
    return llvm::ConstantInt::get(i32Ty(), 0);
  }
  if (!struct_ret) {
    return call_val;
  }
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* slot = createEntryAlloca(fn, callee->getReturnType(), "scoped.ret");
  builder_.CreateStore(call_val, slot);
  return slot;
}

// `a::b::X.m()` / aliased `X.m()`: the member is an exported free function of the
// namespace chain (`use net::http::Server` makes `Server.start()` reach
// `net::http::Server::start`).
llvm::Value* Codegen::emitQualifiedFreeCall(const ast::MethodCallExpr& call,
                                            const ast::IdentifierExpr& id) {
  const std::string scope = expandAliasedScope(use_aliases_, id.name);
  return emitExportedCall(scope, call.args, scope + "::" + call.method, call.loc);
}

// A namespace export reached through a `.` receiver: same ABI as a free call.
llvm::Value* Codegen::emitMethodAsFreeCall(const ast::FuncDeclStmt& fn,
                                           const ast::MethodCallExpr& call) {
  return emitExportedCall(fn.module_name, call.args,
                          fn.module_name.empty() ? fn.name : fn.module_name + "::" + fn.name,
                          call.loc);
}

llvm::Value* Codegen::emitScopedCall(const ast::ScopedCallExpr& call) {
  // `use pkg::Config`-style class aliases (`Cfg(...)`) and namespaced
  // constructors (`pkg::Config(1)`) both build the class instance directly.
  std::string class_name;
  auto alias_it = use_aliases_.find(call.module);
  if (alias_it != use_aliases_.end() && call.path.size() == 1) {
    class_name = alias_it->second + "::" + call.callee;
  } else if (ensureTypeAvailable(call.module + "::" + call.callee)) {
    class_name = call.module + "::" + call.callee;
  }
  if (!class_name.empty() && isClassType(class_name)) {
    const ClassInfo* info = findClass(class_name);
    if (!info) {
      error(call.loc, "unknown class: " + class_name);
      return nullptr;
    }
    ast::NewExpr neu;
    neu.loc = call.loc;
    neu.class_name = class_name;
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::AllocaInst* tmp = createEntryAlloca(fn, info->type, call.callee + ".tmp");
    llvm::Value* this_ptr = builder_.CreateBitCast(tmp, ptrTy(), "scoped.this");
    if (!emitClassInitCall(*info, neu, this_ptr, call.args)) {
      return nullptr;
    }
    return tmp;
  }
  const std::string scope = expandAliasedScope(use_aliases_, call.module);
  return emitExportedCall(scope, call.args, scope + "::" + call.callee, call.loc);
}

}  // namespace l::codegen
