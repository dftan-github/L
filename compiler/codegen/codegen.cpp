#include "codegen/codegen.hpp"

#include <cstdint>
#include <iostream>
#include <iterator>
#include <utility>

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
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

// Logical field `age` → physical LLVM / field_index key `age_` ([no-trailing-underscore]).
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
                                      const std::string& to_ty) {
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
  error("cannot promote '" + from_ty + "' to '" + to_ty + "'");
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
  return name;
}

std::string Codegen::mangleName(const std::string& module_name, const std::string& name,
                                bool is_pub) {
  // Entry point and pub symbols keep a stable unprefixed name for cross-module calls.
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

llvm::FunctionType* Codegen::llvmFunctionTypeForFunName(const std::string& fun_type_name) {
  std::vector<std::string> params;
  std::string ret;
  bool has_ret = false;
  if (!parseFunTypeName(resolveTypeName(fun_type_name), params, ret, has_ret)) {
    error("invalid function type: " + fun_type_name);
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
  return tmp.CreateAlloca(ty, nullptr, name);
}

llvm::Function* Codegen::requireExtern(const std::string& name) {
  if (llvm::Function* existing = module_->getFunction(name)) {
    return existing;
  }
  error("undeclared external function '" + name +
        "' (declare it with `extern module` in stdlib)");
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
  // [delegate]: unambiguous delegate promotion for typing.
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

std::string Codegen::exprTypeOf(const ast::Expr& expr) const {  switch (expr.kind()) {
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
          (bin.op == "+" || bin.op == "-" || bin.op == "*" || bin.op == "/")) {
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
      auto it = concrete_funcs_.find(call.callee);
      if (it == concrete_funcs_.end() && !current_module_.empty()) {
        it = concrete_funcs_.find(current_module_ + "::" + call.callee);
      }
      if (it != concrete_funcs_.end() && it->second) {
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
      return {};
    }
    case ast::ExprKind::MethodCall: {
      const auto& call = static_cast<const ast::MethodCallExpr&>(expr);
      if (!call.object) {
        return {};
      }
      // Receiver type: a bare identifier may name a class (static/assoc call),
      // otherwise take the expression's own static type. Chained calls such as
      // `v.as_string().unwrap()` resolve through the inner MethodCall.
      if (call.object->kind() != ast::ExprKind::Identifier) {
        const std::string recv_ty = exprTypeOf(*call.object);
        if (recv_ty.empty()) {
          return {};
        }
        return methodReturnTypeOn(recv_ty, call.method);
      }
      const auto& oid = static_cast<const ast::IdentifierExpr&>(*call.object);
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
            if (!field_ty.empty()) {
              return methodReturnTypeOn(field_ty, call.method);
            }
          }
        }
      }
      // ClassName.staticMethod / Variant.from_i32 (free assoc) return types.
      if (lit == local_types_.end()) {
        if (isClassType(oid.name) ||
            (const_cast<Codegen*>(this)->ensureTypeAvailable(oid.name) &&
             isClassType(oid.name))) {
          const ClassInfo* cinfo = findClass(oid.name);
          if (cinfo && cinfo->ast) {
            for (const auto& m : cinfo->ast->methods) {
              if (m && m->name == call.method && m->is_static) {
                return resolveTypeName(m->return_type);
              }
            }
          }
          auto fit = concrete_funcs_.find(call.method);
          if (fit != concrete_funcs_.end() && fit->second) {
            return resolveTypeName(fit->second->return_type);
          }
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
      return methodReturnTypeOn(lit->second, call.method);
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
      const std::string obj_ty = exprTypeOf(*access.object);
      if (!isClassType(obj_ty)) {
        return {};
      }
      const ClassInfo* info = findClass(obj_ty);
      if (!info) {
        return {};
      }
      const PropResolve pr = resolveProperty(*info, access.member, PropAccess::Read);
      if (pr.kind == PropResolveKind::Method && info->ast) {
        for (const auto& m : info->ast->methods) {
          if (m && m->name == pr.method_name) {
            return instantiateMemberType(m->return_type, *info);
          }
        }
        return {};
      }
      if (pr.kind == PropResolveKind::Field) {
        auto fit = info->field_index.find(pr.field_name);
        if (fit == info->field_index.end()) {
          return {};
        }
        return info->field_types[fit->second];
      }
      return {};
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
    error("unknown generic class template: " + base);
    return false;
  }
  return specializeClass(*it->second, args, name);
}

bool Codegen::specializeClass(const ast::ClassDeclStmt& tmpl,
                              const std::vector<std::string>& args,
                              const std::string& instance_name) {
  if (classes_.count(instance_name) != 0) {
    return true;
  }
  if (tmpl.generics.size() != args.size()) {
    error("generic arity mismatch specializing " + tmpl.name);
    return false;
  }

  std::unordered_map<std::string, std::string> subst;
  for (size_t i = 0; i < tmpl.generics.size(); ++i) {
    subst[tmpl.generics[i].name] = args[i];
    if (!tmpl.generics[i].constraint.empty()) {
      if (!ensureTypeAvailable(args[i]) || !isClassType(args[i])) {
        error("constrained generic " + tmpl.generics[i].name + " requires a class type");
        return false;
      }
      if (!classImplements(args[i], tmpl.generics[i].constraint)) {
        error("Type '" + args[i] + "' does not implement interface '" +
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

  std::vector<llvm::Type*> elements;
  unsigned idx = 0;
  for (const auto& field : tmpl.fields) {
    if (!field) {
      continue;
    }
    const std::string fty_name = resolveTypeName(field->type_name);
    if (isClassType(fty_name)) {
      if (!ensureTypeAvailable(fty_name)) {
        type_subst_ = std::move(saved_subst);
        return false;
      }
    } else if (isInterfaceType(fty_name)) {
      // ok — fat pointer field
    }
    const std::string phys = physicalFieldName(field->name);
    info.field_index[phys] = idx++;
    info.field_types.push_back(fty_name);
    info.field_is_pub[phys] = field->is_pub;
    if (isClassType(fty_name)) {
      elements.push_back(findClass(fty_name)->type);
    } else if (isInterfaceType(fty_name)) {
      elements.push_back(fatPtrTy());
    } else {
      elements.push_back(llvmTypeForTypeName(fty_name));
    }
  }
  info.type->setBody(elements, /*isPacked=*/false);
  classes_[instance_name] = std::move(info);

  ClassInfo* cinfo = findClassMut(instance_name);
  ensureTypeId(*cinfo);
  for (const auto& method : tmpl.methods) {
    if (!method) {
      continue;
    }
    if (cinfo->methods.count(method->name) != 0) {
      error("duplicate method in specialized class " + instance_name);
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

  // Specialization may run mid-function (e.g. first Array<i32> local); preserve IRBuilder.
  auto saved_named = named_values_;
  auto saved_locals = local_types_;
  auto saved_heap = heap_locals_;
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

  named_values_ = std::move(saved_named);
  local_types_ = std::move(saved_locals);
  heap_locals_ = std::move(saved_heap);
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

ast::SourceLocation Codegen::defaultLoc() const {
  ast::SourceLocation loc;
  loc.filename = module_ ? module_->getModuleIdentifier() : std::string("<codegen>");
  loc.line = 1;
  loc.column = 1;
  return loc;
}

void Codegen::error(const std::string& message) {
  error(defaultLoc(), message);
}

void Codegen::error(const ast::SourceLocation& loc, const std::string& message) {
  had_error_ = true;
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

bool Codegen::emitArrayOwnedCleanup(const std::string& name, const ClassInfo& info) {
  auto data_fit = info.field_index.find("data_");
  auto len_fit = info.field_index.find("length_");
  if (data_fit == info.field_index.end() || len_fit == info.field_index.end()) {
    error("Array cleanup missing data_/length_ on " + name);
    return false;
  }
  llvm::Function* free_fn = requireExtern("l_free");
  if (!free_fn) {
    return false;
  }
  llvm::Value* arr = classObjectPtr(name);
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
      error("auto-free unknown Array local: " + name);
      return false;
    }
    llvm::Value* obj = builder_.CreateLoad(ptrTy(), vit->second, name + ".arr.obj");
    builder_.CreateCall(free_fn, {obj});
  }
  return true;
}

bool Codegen::emitFatClosureCleanup(const std::string& name) {
  auto vit = named_values_.find(name);
  if (vit == named_values_.end()) {
    error("fat-closure cleanup missing state for '" + name + "'");
    return false;
  }
  llvm::Function* free_fn = requireExtern("l_free");
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

bool Codegen::emitLFreeOfLocal(const std::string& name) {
  if (fat_closure_locals_.count(name) != 0) {
    return emitFatClosureCleanup(name);
  }
  auto lit = local_types_.find(name);
  if (lit != local_types_.end()) {
    const ClassInfo* info = findClass(lit->second);
    if (info && info->template_name == "Array") {
      return emitArrayOwnedCleanup(name, *info);
    }
  }

  auto vit = named_values_.find(name);
  if (vit == named_values_.end()) {
    error("auto-free unknown local: " + name);
    return false;
  }
  llvm::Value* ptr = builder_.CreateLoad(ptrTy(), vit->second, name + ".autofree");
  if (arena_locals_.count(name) != 0) {
    llvm::Function* free_fn = requireExtern("l_arena_free");
    if (!free_fn) {
      return false;
    }
    builder_.CreateCall(free_fn, {ptr});
    arena_locals_.erase(name);
    return true;
  }
  llvm::Function* free_fn = requireExtern("l_free");
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
    if (!emitLFreeOfLocal(*it)) {
      return false;
    }
  }
  names.clear();
  return true;
}

bool Codegen::flushAllAutoFrees() {
  for (auto sit = auto_free_scopes_.rbegin(); sit != auto_free_scopes_.rend(); ++sit) {
    for (auto it = sit->rbegin(); it != sit->rend(); ++it) {
      if (!emitLFreeOfLocal(*it)) {
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
  llvm::Function* unlock = requireExtern("l_mutex_unlock");
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

llvm::Value* Codegen::classObjectPtr(const std::string& name) {
  auto it = named_values_.find(name);
  if (it != named_values_.end()) {
    if (heap_locals_.count(name) != 0) {
      return builder_.CreateLoad(ptrTy(), it->second, name + ".obj");
    }
    return it->second;
  }
  auto git = global_vars_.find(name);
  if (git != global_vars_.end()) {
    auto gty = global_types_.find(name);
    if (gty != global_types_.end() && isClassType(gty->second)) {
      return git->second;  // GlobalVariable* decays to i8*/Class* address
    }
  }
  error("unknown variable: " + name);
  return nullptr;
}

Codegen::PropResolve Codegen::resolveProperty(const ClassInfo& info, const std::string& prop,
                                              PropAccess access) const {
  PropResolve out;
  // Trailing `_` identifiers are forbidden in source ([no-trailing-underscore]); bare `_` is not a property.
  if (prop.size() > 1 && prop.back() == '_') {
    return out;
  }

  // Property sugar: prefer get_x / set_x.
  const std::string method =
      (access == PropAccess::Write) ? ("set_" + prop) : ("get_" + prop);
  if (info.methods.count(method) != 0) {
    out.kind = PropResolveKind::Method;
    out.method_name = method;
    return out;
  }

  // Fallback: hidden physical field `prop_` (any visibility).
  const std::string phys = physicalFieldName(prop);
  if (info.field_index.count(phys) != 0) {
    out.kind = PropResolveKind::Field;
    out.field_name = phys;
    return out;
  }
  return out;
}

llvm::Value* Codegen::classValuePtr(const ast::Expr& expr, const std::string& class_name) {
  if (!ensureTypeAvailable(class_name)) {
    error("unknown class: " + class_name);
    return nullptr;
  }
  const ClassInfo* info = findClass(class_name);
  if (!info) {
    error("unknown class: " + class_name);
    return nullptr;
  }
  if (expr.kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
    return classObjectPtr(id.name);
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
  error("expected class value of type " + class_name);
  return nullptr;
}

bool Codegen::registerInterface(const ast::InterfaceDeclStmt& decl) {
  if (interfaces_.count(decl.name) != 0) {
    error("redefinition of interface: " + decl.name);
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
    error("redefinition of class: " + decl.name);
    return false;
  }

  ClassInfo info;
  info.ir_name = mangleName(decl.module_name, decl.name, decl.is_pub);
  info.type = llvm::StructType::create(globalContext(), info.ir_name);
  info.interfaces = decl.interfaces;
  info.ast = &decl;

  std::vector<llvm::Type*> elements;
  unsigned idx = 0;
  for (const auto& field : decl.fields) {
    if (!field) {
      continue;
    }
    const std::string phys = physicalFieldName(field->name);
    if (info.field_index.count(phys) != 0) {
      error("duplicate field '" + field->name + "' in class " + decl.name);
      return false;
    }
    const std::string fty_name = field->type_name;
    // A field may name a generic instantiation (`Map<Variant>`) nobody has
    // materialized yet — materialize it so the field embeds the real struct.
    if (fty_name.find('<') != std::string::npos && !isClassType(fty_name)) {
      if (!ensureTypeAvailable(fty_name)) {
        return false;
      }
    }
    if (isClassType(fty_name)) {
      if (!ensureTypeAvailable(fty_name)) {
        return false;
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
    info.field_index[phys] = idx++;
    info.field_types.push_back(fty_name);
    info.field_is_pub[phys] = field->is_pub;
    elements.push_back(fty);
  }
  info.type->setBody(elements, /*isPacked=*/false);
  classes_[decl.name] = std::move(info);
  ensureTypeId(*findClassMut(decl.name));
  return true;
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
      error("duplicate method in class " + decl.name + ": " + method->name);
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
        error("cannot resolve return type '" + method->return_type + "' of " + decl.name + "." +
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

bool Codegen::buildVtables(const ast::ClassDeclStmt& decl) {
  if (!decl.generics.empty()) {
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
      error("class " + decl.name + " implements unknown interface " + iface_name);
      return false;
    }
    std::vector<llvm::Constant*> slots;
    slots.push_back(tid);  // index 0: TypeID (unique address)
    for (const std::string& method_name : iface->method_names) {
      auto mit = info->methods.find(method_name);
      if (mit == info->methods.end()) {
        error("class " + decl.name + " missing method '" + method_name +
              "' for interface " + iface_name);
        return false;
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

bool Codegen::emitAutoCloneBody(const std::string& class_name, llvm::Function* fn) {
  ClassInfo* info = findClassMut(class_name);
  if (!info || !info->type) {
    error("auto clone: unknown class " + class_name);
    return false;
  }
  llvm::Value* self = &*fn->arg_begin();
  // Phase 1: treat the whole object as a value — load/store is a bitwise copy.
  // Nested class fields embedded by value are copied; raw ptr / iface slots stay shallow.
  llvm::Value* copy = builder_.CreateLoad(info->type, self, "clone.copy");
  if (!fn->getReturnType()->isStructTy()) {
    error("auto clone return type must be the class value for " + class_name);
    return false;
  }
  builder_.CreateRet(copy);
  return true;
}

llvm::Value* Codegen::makeStringLitObject(const std::string& text, const std::string& name) {
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error("String type not available for to_string");
    return nullptr;
  }
  auto init_it = info->methods.find("init");
  if (init_it == info->methods.end()) {
    error("String.init not found for to_string");
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

llvm::Value* Codegen::concatStringObjects(llvm::Value* lhs, llvm::Value* rhs) {
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error("String type not available for to_string concat");
    return nullptr;
  }
  auto mit = info->methods.find("concat");
  if (mit == info->methods.end()) {
    error("String.concat not found for to_string");
    return nullptr;
  }
  llvm::Value* by_val = builder_.CreateCall(mit->second, {lhs, rhs}, "ts.concat");
  llvm::Function* cur = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* tmp = createEntryAlloca(cur, info->type, "ts.concat.tmp");
  builder_.CreateStore(by_val, tmp);
  return tmp;
}

bool Codegen::emitAutoToStringBody(const std::string& class_name, llvm::Function* fn) {
  ClassInfo* info = findClassMut(class_name);
  if (!info || !info->type) {
    error("auto to_string: unknown class " + class_name);
    return false;
  }
  if (!ensureTypeAvailable("String")) {
    error("auto to_string requires String");
    return false;
  }
  const ClassInfo* str_info = findClass("String");
  if (!str_info) {
    error("String type not available for auto to_string");
    return false;
  }
  if (!fn->getReturnType()->isStructTy() || fn->getReturnType() != str_info->type) {
    error("auto to_string must return String by value");
    return false;
  }

  llvm::Value* self = &*fn->arg_begin();
  const std::string display =
      !info->template_name.empty() ? info->template_name
                                   : (info->ast ? info->ast->name : class_name);

  llvm::Value* acc = makeStringLitObject(display + " { ", "ts.head");
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
      llvm::Value* sep = makeStringLitObject(", ", "ts.sep");
      if (!sep) {
        return false;
      }
      acc = concatStringObjects(acc, sep);
      if (!acc) {
        return false;
      }
    }

    const std::string logical = displayFieldName(fname);
    llvm::Value* label = makeStringLitObject(logical + ": ", "ts.lab");
    if (!label) {
      return false;
    }
    acc = concatStringObjects(acc, label);
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
      llvm::Value* t_obj = makeStringLitObject("true", "ts.true");
      llvm::Value* f_obj = makeStringLitObject("false", "ts.false");
      if (!t_obj || !f_obj) {
        return false;
      }
      // Both are String*; pick pointer via select.
      piece = builder_.CreateSelect(v, t_obj, f_obj, "ts.bool");
    } else if (fty == "String") {
      // Embedded String by value — address is a String*.
      piece = gep;
    } else if (fty == "f64") {
      piece = makeStringLitObject("<f64>", "ts.f64");
    } else if (fty == "ptr") {
      piece = makeStringLitObject("<ptr>", "ts.ptr");
    } else {
      // Nested class / interface / unknown — placeholder.
      piece = makeStringLitObject("<Object>", "ts.obj");
    }
    if (!piece) {
      return false;
    }
    acc = concatStringObjects(acc, piece);
    if (!acc) {
      return false;
    }
  }

  llvm::Value* tail = makeStringLitObject(" }", "ts.tail");
  if (!tail) {
    return false;
  }
  acc = concatStringObjects(acc, tail);
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
    error("bad method function for " + class_name + "." + method.name);
    return false;
  }

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(globalContext(), "entry", fn);
  builder_.SetInsertPoint(entry);
  named_values_.clear();
  local_types_.clear();
  heap_locals_.clear();
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
    const bool ok = emitAutoCloneBody(class_name, fn);
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
      error("method verification failed: " + class_name + "." + method.name);
      fn->eraseFromParent();
      return false;
    }
    return true;
  }

  if (method.is_compiler_generated && method.name == "to_string") {
    const bool ok = emitAutoToStringBody(class_name, fn);
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
      error("method verification failed: " + class_name + "." + method.name);
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
      error("missing return in method " + class_name + "." + method.name);
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
    error("method verification failed: " + class_name + "." + method.name);
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
  classes_.clear();
  interfaces_.clear();
  generic_templates_.clear();
  concrete_funcs_.clear();
  generic_class_templates_.clear();
  specialized_funcs_.clear();
  type_subst_.clear();
  heap_locals_.clear();
  arena_locals_.clear();
  readonly_refs_.clear();
  defer_scopes_.clear();
  auto_free_scopes_.clear();
  public_funcs_.clear();
  private_funcs_.clear();
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

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::InterfaceDecl) {
      continue;
    }
    if (!registerInterface(static_cast<const ast::InterfaceDeclStmt&>(*stmt))) {
      return false;
    }
  }

  // extern module FIRST: class structs may embed generic instantiations
  // (`Map<Variant>` field), and their specialized bodies call l_alloc / l_realloc.
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ExternModule) {
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
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    const auto& cls = static_cast<const ast::ClassDeclStmt&>(*stmt);
    if (cls.generics.empty()) {
      continue;
    }
    if (generic_class_templates_.count(cls.name) != 0) {
      error("redefinition of generic class: " + cls.name);
      return false;
    }
    generic_class_templates_[cls.name] = &cls;
  }

  // `String` is a dependency of nearly every stdlib module, so its struct +
  // methods go first. Only then can generic instantiations that use it
  // (`Map<Variant>` → `keys()`) emit method bodies cleanly.
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

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::ClassDecl) {
      continue;
    }
    const auto& cls = static_cast<const ast::ClassDeclStmt&>(*stmt);
    if (!cls.generics.empty()) {
      continue;  // template registered above
    }
    if (cls.name == "String") {
      continue;  // struct + methods already emitted above
    }
    if (!emitClassStruct(cls)) {
      return false;
    }
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
    // Compile-time only: executed by the LCMD interpreter, never lowered to LLVM.
    if (fn.name == "__compile_time_handler" || fn.name == "__on_class_parsed") {
      continue;
    }
    const std::string ir_name = mangleName(fn.module_name, fn.name, fn.is_pub);
    if (fn.is_pub || fn.name == "main") {
      public_funcs_[fn.name] = ir_name;
    } else {
      private_funcs_[fn.module_name + "::" + fn.name] = ir_name;
    }
    if (!fn.generics.empty()) {
      const std::string tmpl_key =
          fn.is_pub ? fn.name : (fn.module_name + "::" + fn.name);
      if (generic_templates_.count(tmpl_key) != 0) {
        error("redefinition of generic function: " + fn.name);
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
    if (!fn.returns_fat_closure && !fn.return_type.empty() && fn.return_type != "void") {
      if (!ensureTypeAvailable(fn.return_type)) {
        error("cannot resolve return type '" + fn.return_type + "' of " + fn.name);
        return false;
      }
    }
    // Escaping capturing lambda → ABI is fat closure {fn,ctx}, not a bare fun pointer.
    llvm::Type* ret_ty =
        fn.returns_fat_closure ? static_cast<llvm::Type*>(fatClosureTy())
                               : typeFromName(fn.return_type);
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

  // Module-level variables before any function body (GlobalVariable / const inline).
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::VarDecl) {
      continue;
    }
    if (!emitGlobalVarDecl(static_cast<const ast::VarDeclStmt&>(*stmt))) {
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
  }

  if (llvm::verifyModule(*module_, &llvm::errs())) {
    error("module verification failed");
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
    error("lookupTarget failed: " + err);
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
    error("failed to create TargetMachine");
    return false;
  }

  module_->setDataLayout(tm->createDataLayout());
  optimizeModule(tm.get());

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
    case ast::StmtKind::DestructureDecl:
      return emitDestructureDecl(static_cast<const ast::DestructureDeclStmt&>(stmt));
    case ast::StmtKind::Assign:
      return emitAssign(static_cast<const ast::AssignStmt&>(stmt));
    case ast::StmtKind::Return:
      return emitReturn(static_cast<const ast::ReturnStmt&>(stmt));
    case ast::StmtKind::ExprStmt:
      return emitExprStmt(static_cast<const ast::ExprStmt&>(stmt));
    case ast::StmtKind::Block:
      return emitStmtListScoped(static_cast<const ast::BlockStmt&>(stmt).stmts);
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
    error("lifecycle hook verification failed: " + fn->getName().str());
    fn->eraseFromParent();
    return false;
  }
  return true;
}

bool Codegen::emitPendingGlobalClassInits() {
  for (const PendingGlobalClassInit& p : pending_global_class_inits_) {
    if (!p.neu) {
      error("global class '" + p.name + "' missing NewExpr initializer");
      return false;
    }
    if (!ensureTypeAvailable(p.class_name)) {
      error("unknown class for global '" + p.name + "': " + p.class_name);
      return false;
    }
    const ClassInfo* info = findClass(p.class_name);
    auto git = global_vars_.find(p.name);
    if (!info || git == global_vars_.end()) {
      error("global class '" + p.name + "' not registered");
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
      if (!stmt || !isModuleTopLevelExecStmt(*stmt)) {
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
      error("lifecycle hook verification failed: __l_global_init");
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
  const std::string ir_name = mangleName(fn_ast.module_name, fn_ast.name, fn_ast.is_pub);
  llvm::Function* fn = module_->getFunction(ir_name);
  if (!fn) {
    error("missing function declaration: " + ir_name);
    return false;
  }
  if (!fn->empty()) {
    error("redefinition of function: " + ir_name);
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
  enterDeferScope();

  if (is_main) {
    // LLVM main is always (i32 argc, ptr argv); stash when extern l_init_args is present.
    if (fn->arg_size() != 2) {
      error("internal: main must be declared as (i32, ptr) at the LLVM layer");
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
      error("main must have zero parameters or (i32 argc, ptr argv)");
      return false;
    }
  } else {
    unsigned idx = 0;
    for (auto& arg : fn->args()) {
      if (idx >= fn_ast.params.size()) {
        error("internal: argument count mismatch for " + fn_ast.name);
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
      error("missing return in non-i32 function: " + fn_ast.name);
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
    error("function verification failed: " + fn_ast.name);
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

  // [delegate]: embedded fields are anonymous class instances; construct each
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
        llvm::Function* alloc_fn = requireExtern("l_alloc");
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
    llvm::Function* init_fn = init_it->second;
    const ast::FuncDeclStmt* init_ast = nullptr;
    if (info.ast) {
      for (const auto& m : info.ast->methods) {
        if (m && m->name == "init") {
          init_ast = m.get();
          break;
        }
      }
    }
    if (init_fn->arg_size() != neu.args.size() + 1) {
      error("argument count mismatch calling " + neu.class_name + ".init");
      return false;
    }
    std::vector<llvm::Value*> call_args;
    call_args.push_back(base);  // this*
    unsigned ai = 1;
    for (const ast::CallArg& arg : neu.args) {
      if (!arg.value) {
        error("null argument to " + neu.class_name + ".init");
        return false;
      }
      if (!arg.name.empty()) {
        error("keyword arguments not supported in constructors yet");
        return false;
      }
      llvm::Type* expected = init_fn->getFunctionType()->getParamType(ai);
      std::string expected_name;
      if (init_ast && ai - 1 < init_ast->params.size()) {
        expected_name = resolveTypeName(init_ast->params[ai - 1].type_name);
      }
      ++ai;
      llvm::Value* v = emitValueForParam(*arg.value, expected, expected_name);
      if (!v) {
        return false;
      }
      call_args.push_back(v);
    }
    builder_.CreateCall(init_fn, call_args);
    return true;
  }

  // Legacy fallback: map NewExpr args onto fields in declaration order.
  if (neu.args.size() > info.field_types.size()) {
    error("too many constructor arguments for " + neu.class_name);
    return false;
  }
  for (size_t i = 0; i < neu.args.size(); ++i) {
    if (!neu.args[i].value) {
      continue;
    }
    if (!neu.args[i].name.empty()) {
      error("keyword arguments not supported in field-order constructors");
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
        v = toI32(v);
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
  llvm::Function* global_fn = requireExtern("l_mutex_global");
  llvm::Function* lock_fn = requireExtern("l_mutex_lock");
  llvm::Function* unlock_fn = requireExtern("l_mutex_unlock");
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
  llvm::Function* spawn_rt = requireExtern("l_spawn");
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

  std::unordered_map<std::string, llvm::AllocaInst*> outer_slots;
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
        llvm::Function* free_fn = requireExtern("l_free");
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
    llvm::Function* alloc_fn = requireExtern("l_alloc");
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
      llvm::Value* gep =
          builder_.CreateStructGEP(ctx_ty, ctx_ptr, i, caps[i] + ".cap.slot");
      builder_.CreateStore(slot, gep);
    }
  }

  llvm::Value* fn_ptr = builder_.CreateBitCast(worker, ptrTy(), "spawn.fn");
  return builder_.CreateCall(spawn_rt, {fn_ptr, ctx_ptr}, "spawn.handle");
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
  llvm::Function* alloc = requireExtern("l_alloc");
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
    return boxToInterface(obj, from_ty, to_ty);
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
    return toI32(v);
  }
  error(expr.loc, "unsupported cast from '" + from_ty + "' to '" + to_ty + "'");
  return nullptr;
}

std::string Codegen::inferTypeFromExpr(const ast::Expr& expr) {
  // Bare `"text"` in `:=` contexts is a String when the stdlib type is loaded.
  if (expr.kind() == ast::ExprKind::StringLiteral) {
    if (ensureTypeAvailable("String") && isClassType("String")) {
      return "String";
    }
    return "ptr";
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
        error(b.loc, "unknown field class type '" + fty_name + "'");
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
      error("inferred declaration requires an initializer: " + decl.name);
      return false;
    }
    type_name = inferTypeFromExpr(*decl.init);
    if (type_name.empty()) {
      error("cannot infer type for '" + decl.name + "' from initializer");
      return false;
    }
  }

  if (!type_name.empty()) {
    ensureTypeAvailable(type_name);
  }

  if (type_name == "Arena") {
    llvm::AllocaInst* slot = createEntryAlloca(fn, ptrTy(), decl.name);
    if (!decl.init) {
      error("Arena '" + decl.name + "' requires an initializer");
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
    local_types_[decl.name] = type_name;

    if (decl.init && decl.init->kind() == ast::ExprKind::Heap) {
      const auto& heap = static_cast<const ast::HeapExpr&>(*decl.init);
      if (!heap.inner || heap.inner->kind() != ast::ExprKind::New) {
        error("heap init requires ClassName(...)");
        return false;
      }
      const auto& neu = static_cast<const ast::NewExpr&>(*heap.inner);
      if (neu.class_name != type_name) {
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
      noteAutoFreeLocal(decl.name);
      return true;
    }

    // Arena bump-owned class* — accessible as heap local, but never individually l_free'd.
    if (decl.init && isArenaOwnedClassExpr(*decl.init)) {
      llvm::AllocaInst* slot = createEntryAlloca(fn, ptrTy(), decl.name);
      llvm::Value* ptr = emitExpr(*decl.init);
      if (!ptr) {
        return false;
      }
      if (!ptr->getType()->isPointerTy()) {
        error("arena-owned init for '" + decl.name + "' expected a pointer");
        return false;
      }
      builder_.CreateStore(ptr, slot);
      named_values_[decl.name] = slot;
      heap_locals_.insert(decl.name);
      return true;
    }

    // String `a + b` (and similar) yields an owning heap String* — track for auto-free.
    if (decl.init && isOwnedHeapClassExpr(*decl.init)) {
      llvm::AllocaInst* slot = createEntryAlloca(fn, ptrTy(), decl.name);
      llvm::Value* heap_ptr = emitExpr(*decl.init);
      if (!heap_ptr) {
        return false;
      }
      if (!heap_ptr->getType()->isPointerTy()) {
        error("owned heap init for '" + decl.name + "' expected a pointer");
        return false;
      }
      builder_.CreateStore(heap_ptr, slot);
      named_values_[decl.name] = slot;
      heap_locals_.insert(decl.name);
      noteAutoFreeLocal(decl.name);
      return true;
    }

    // `c := opaque_ptr as Counter` — borrowed Class* handle (no auto-free).
    if (decl.init && decl.init->kind() == ast::ExprKind::Cast) {
      const auto& cast = static_cast<const ast::CastExpr&>(*decl.init);
      if (cast.value && isClassType(cast.type_name) && cast.type_name == type_name) {
        std::string from_ty = inferTypeFromExpr(*cast.value);
        if (from_ty.empty()) {
          from_ty = exprTypeOf(*cast.value);
        }
        if (from_ty == "ptr") {
          llvm::AllocaInst* slot = createEntryAlloca(fn, ptrTy(), decl.name);
          llvm::Value* obj = emitExpr(*decl.init);
          if (!obj) {
            return false;
          }
          if (!obj->getType()->isPointerTy()) {
            error("ptr → class cast for '" + decl.name + "' expected a pointer");
            return false;
          }
          builder_.CreateStore(obj, slot);
          named_values_[decl.name] = slot;
          heap_locals_.insert(decl.name);
          return true;
        }
      }
    }

    llvm::AllocaInst* alloca = createEntryAlloca(fn, info->type, decl.name);
    named_values_[decl.name] = alloca;
    heap_locals_.erase(decl.name);
    if (decl.init) {
      if (decl.init->kind() == ast::ExprKind::New) {
        const auto& neu = static_cast<const ast::NewExpr&>(*decl.init);
        if (neu.class_name != type_name) {
          error("class type mismatch in initialization of " + decl.name);
          return false;
        }
        if (!initClassFromNew(alloca, *info, neu)) {
          return false;
        }
        // Stack Array owns its data_ buffer (+ boxed iface elems) → free on scope exit.
        if (info->template_name == "Array") {
          noteAutoFreeLocal(decl.name);
        }
        return true;
      }
      // `s := "hello"` → stack String initialized from a C string literal.
      if (type_name == "String" && decl.init->kind() == ast::ExprKind::StringLiteral) {
        auto init_it = info->methods.find("init");
        if (init_it == info->methods.end()) {
          error("String.init not found");
          return false;
        }
        for (size_t i = 0; i < info->field_types.size(); ++i) {
          llvm::Type* fty = llvmTypeForTypeName(info->field_types[i]);
          llvm::Value* gep =
              builder_.CreateStructGEP(info->type, alloca, static_cast<unsigned>(i), "str.z");
          builder_.CreateStore(llvm::Constant::getNullValue(fty), gep);
        }
        const auto& lit = static_cast<const ast::StringLiteralExpr&>(*decl.init);
        llvm::Value* cstr = builder_.CreateGlobalString(lit.value, "str");
        builder_.CreateCall(init_it->second, {alloca, cstr});
        return true;
      }
      // Allow := from method calls / String+ / other class-typed expressions.
      llvm::Value* v = emitExpr(*decl.init);
      if (!v) {
        return false;
      }
      if (v->getType() == info->type) {
        builder_.CreateStore(v, alloca);
      } else if (v->getType()->isPointerTy()) {
        llvm::Value* loaded = builder_.CreateLoad(info->type, v, decl.name + ".copy");
        builder_.CreateStore(loaded, alloca);
      } else {
        error("cannot initialize class '" + decl.name + "' from non-class expression");
        return false;
      }
    }
    return true;
  }

  if (isInterfaceType(type_name)) {
    llvm::AllocaInst* alloca = createEntryAlloca(fn, fatPtrTy(), decl.name);
    named_values_[decl.name] = alloca;
    local_types_[decl.name] = type_name;

    if (!decl.init) {
      error("interface variable requires initializer");
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
        decl.init->kind() == ast::ExprKind::Index) {
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
        error("interface init expects a class instance");
        return false;
      }
      class_ty = lit->second;
      obj = classObjectPtr(id.name);
    } else if (decl.init->kind() == ast::ExprKind::New) {
      const auto& neu = static_cast<const ast::NewExpr&>(*decl.init);
      class_ty = neu.class_name;
      obj = heapAllocNewExpr(neu);
    } else {
      class_ty = inferTypeFromExpr(*decl.init);
      if (!isClassType(class_ty)) {
        error("interface init expects a class instance");
        return false;
      }
      obj = classValuePtr(*decl.init, class_ty);
    }
    if (!obj) {
      return false;
    }
    llvm::Value* fat = boxToInterface(obj, class_ty, type_name);
    if (!fat) {
      return false;
    }
    builder_.CreateStore(fat, alloca);
    return true;
  }

  // First-class function local: `double := |x| x * 2` / `fun(i32)->i32 f = ...`
  // Escaped returns: `cb := get_closure()` stores fat `{fn,ctx}` ([closure-escape]).
  if (isFunTypeName(type_name)) {
    if (!decl.init) {
      error("function variable '" + decl.name + "' requires an initializer");
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
      llvm::AllocaInst* alloca = createEntryAlloca(fn, fatClosureTy(), decl.name);
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

    llvm::AllocaInst* alloca = createEntryAlloca(fn, ptrTy(), decl.name);
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
  llvm::AllocaInst* alloca = createEntryAlloca(fn, lty, decl.name);
  llvm::Value* init = nullptr;
  if (decl.init) {
    init = emitExpr(*decl.init);
    if (!init) {
      return false;
    }
    if (lty->isIntegerTy(32) && !init->getType()->isPointerTy()) {
      init = toI32(init);
    } else if (lty->isIntegerTy(64) && init->getType()->isIntegerTy()) {
      init = builder_.CreateIntCast(init, i64Ty(), /*isSigned=*/true, "init.i64");
    } else if (lty->isDoubleTy()) {
      if (init->getType()->isIntegerTy()) {
        init = builder_.CreateSIToFP(init, f64Ty(), "init.f64");
      } else if (!init->getType()->isDoubleTy()) {
        error("cannot initialize f64 '" + decl.name + "' from non-numeric expression");
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
    error("incomplete index expression");
    return nullptr;
  }

  // Array<T> / specialized class: arr[i] → GEP on data_ with elem = T
  if (expr.object->kind() == ast::ExprKind::Identifier) {
    const auto& id = static_cast<const ast::IdentifierExpr&>(*expr.object);
    auto lit = local_types_.find(id.name);
    if (lit != local_types_.end() && isClassType(lit->second)) {
      const ClassInfo* info = findClass(lit->second);
      if (!info || info->type_args.empty()) {
        error("index on non-generic class " + lit->second);
        return nullptr;
      }
      auto fit = info->field_index.find("data_");
      if (fit == info->field_index.end()) {
        error("indexed class missing data_ field");
        return nullptr;
      }
      llvm::Value* obj = classObjectPtr(id.name);
      if (!obj) {
        return nullptr;
      }
      llvm::Value* data_gep =
          builder_.CreateStructGEP(info->type, obj, fit->second, "arr.data");
      llvm::Value* data = builder_.CreateLoad(ptrTy(), data_gep, "arr.data.ptr");
      llvm::Value* idx = toI32(emitExpr(*expr.index));
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
    error("index base must be a pointer or Array");
    return nullptr;
  }
  llvm::Type* elem_ty = elementTypeForIndexedBase(*expr.object);
  if (!elem_ty) {
    error("cannot determine element type for pointer index (need generic class context)");
    return nullptr;
  }
  llvm::Value* idx = toI32(emitExpr(*expr.index));
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

llvm::Value* Codegen::emitLValue(const ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);

      if (const_values_.count(id.name) != 0) {
        error("cannot assign to const '" + id.name + "'");
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
        error("cannot use 'this' inside a static method");
        return nullptr;
      }
      // Method body: bare field name resolves against this (logical → physical).
      if (!current_class_.empty()) {
        const ClassInfo* info = findClass(current_class_);
        if (info) {
          auto fit = info->field_index.find(physicalFieldName(id.name));
          if (fit != info->field_index.end()) {
            if (current_method_is_static_ || !current_this_) {
              error("cannot access instance field '" + id.name + "' from a static method");
              return nullptr;
            }
            return builder_.CreateStructGEP(info->type, current_this_, fit->second, id.name);
          }
        }
      }

      if (llvm::Value* storage = lookupStorage(id.name)) {
        if (global_readonly_.count(id.name) != 0) {
          error("cannot assign to constd '" + id.name + "'");
          return nullptr;
        }
        return storage;
      }
      error("unknown variable: " + id.name);
      return nullptr;
    }
    case ast::ExprKind::MemberAccess: {
      const auto& access = static_cast<const ast::MemberAccessExpr&>(expr);
      if (!access.object) {
        error("member access missing object");
        return nullptr;
      }
      std::string obj_ty = exprTypeOf(*access.object);
      if (obj_ty.empty()) {
        obj_ty = inferTypeFromExpr(*access.object);
      }
      if (!isClassType(obj_ty)) {
        error("member access on non-class value");
        return nullptr;
      }
      ensureTypeAvailable(obj_ty);
      const ClassInfo* info = findClass(obj_ty);
      if (!info) {
        error("bad class object in member access");
        return nullptr;
      }
      const PropResolve pr = resolveProperty(*info, access.member, PropAccess::Write);
      if (pr.kind == PropResolveKind::Method) {
        error(access.loc, "property '" + access.member + "' uses setter; not an lvalue");
        return nullptr;
      }
      if (pr.kind != PropResolveKind::Field) {
        error(access.loc, "unknown property '" + access.member +
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
      error("expression is not an assignable lvalue");
      return nullptr;
  }
}

bool Codegen::emitAssign(const ast::AssignStmt& assign) {
  if (!assign.target || !assign.value) {
    error("incomplete assignment");
    return false;
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
          error("bad class in property assign");
          return false;
        }
        const PropResolve pr = resolveProperty(*info, access.member, PropAccess::Write);
        if (pr.kind == PropResolveKind::Method) {
          auto mit = info->methods.find(pr.method_name);
          if (mit == info->methods.end()) {
            error("missing setter " + pr.method_name);
            return false;
          }
          llvm::Value* self = classValuePtr(*access.object, obj_ty);
          if (!self) {
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
        error(access.loc, "unknown property '" + access.member + "' (no set_" +
                              access.member + " / pub " + access.member + "_)");
        return false;
      }
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
      v = toI32(v);
    } else if (elem_ty && elem_ty->isStructTy() && v->getType()->isPointerTy()) {
      // Class/struct elements (e.g. Array<String>): ABI value is Class*; store by value.
      v = builder_.CreateLoad(elem_ty, v, "idx.store.copy");
    } else if (elem_ty && elem_ty->isPointerTy() && !v->getType()->isPointerTy()) {
      error("pointer index store expects a pointer value");
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
    if (dest_ty && dest_ty->isStructTy() && v->getType() == dest_ty->getPointerTo()) {
      // Inline struct field/element: ABI passes classes by pointer; store by value.
      // Only deref when the value really points at that struct (not a raw cstr).
      v = builder_.CreateLoad(dest_ty, v, "store.struct.copy");
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
    } else if (!v->getType()->isPointerTy() &&
               !(dest_ty && dest_ty->isIntegerTy(64)) &&
               !(dest_ty && dest_ty->isDoubleTy())) {
      v = toI32(v);
    }
  }
  builder_.CreateStore(v, ptr);
  return true;
}

bool Codegen::emitReturn(const ast::ReturnStmt& ret) {
  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::Value* v = nullptr;
  if (ret.value) {
    // [closure-escape]: returning an escaping capturing lambda → fat closure {fn, ctx}.
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
    v = emitExpr(*ret.value);
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
      v = toI32(v);
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
    scrut_val = toI32(scrut_val);
  }
  llvm::AllocaInst* slot = createEntryAlloca(fn, slot_ty, "match.scrut");
  builder_.CreateStore(scrut_val, slot);

  llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(globalContext(), "match.merge", fn);
  bool saw_wildcard = false;

  for (size_t i = 0; i < stmt.branches.size(); ++i) {
    const ast::MatchBranch& br = stmt.branches[i];
    if (saw_wildcard) {
      error(br.loc, "unreachable match arm after '_'");
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

    if (!br.pattern) {
      error(br.loc, "match arm missing pattern");
      return false;
    }

    llvm::BasicBlock* then_bb =
        llvm::BasicBlock::Create(globalContext(), "match.then", fn);
    llvm::BasicBlock* else_bb =
        llvm::BasicBlock::Create(globalContext(), "match.else", fn);

    llvm::Value* loaded = builder_.CreateLoad(slot_ty, slot, "match.v");
    llvm::Value* cond = nullptr;

    if (br.pattern->kind() == ast::ExprKind::IntLiteral) {
      llvm::Value* pat = toI32(emitExpr(*br.pattern));
      if (!pat) {
        return false;
      }
      cond = builder_.CreateICmpEQ(toI32(loaded), pat, "match.eq");
    } else if (br.pattern->kind() == ast::ExprKind::BoolLiteral) {
      llvm::Value* pat = toI1(emitExpr(*br.pattern));
      if (!pat) {
        return false;
      }
      cond = builder_.CreateICmpEQ(toI1(loaded), pat, "match.eq");
    } else if (br.pattern->kind() == ast::ExprKind::StringLiteral) {
      llvm::Function* str_eq = requireExtern("l_str_eq");
      if (!str_eq) {
        return false;
      }
      llvm::Value* lhs = loaded;
      if (scrut_ty == "String") {
        ensureTypeAvailable("String");
        const ClassInfo* info = findClass("String");
        if (!info) {
          error(br.loc, "String type not available for match");
          return false;
        }
        auto fit = info->field_index.find("data_");
        if (fit == info->field_index.end()) {
          error(br.loc, "String.data_ not found");
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
      error(br.loc, "match patterns must be literals or '_'");
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
    error("'break' outside a loop");
    return false;
  }
  flushLoopJumpScopes();
  branchTo(loop_stack_.back().end_bb);
  return true;
}

bool Codegen::emitContinue(const ast::ContinueStmt& stmt) {
  (void)stmt;
  if (loop_stack_.empty()) {
    error("'continue' outside a loop");
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
    error("range for-in missing bounds");
    return false;
  }
  if (named_values_.count(stmt.item_name) != 0) {
    error("for-in loop variable shadows existing name: " + stmt.item_name);
    return false;
  }

  llvm::Function* fn = builder_.GetInsertBlock()->getParent();
  llvm::AllocaInst* item_slot = createEntryAlloca(fn, i32Ty(), stmt.item_name);
  llvm::AllocaInst* end_slot = createEntryAlloca(fn, i32Ty(), stmt.item_name + ".end");
  llvm::Value* start = toI32(emitExpr(*range.left));
  llvm::Value* end = toI32(emitExpr(*range.right));
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
    error("for-in missing iterable");
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
      error("unknown iterable in for-in: " + arr_id.name);
      return false;
    }
    arr_ty = lit->second;
  } else if (stmt.iterable->kind() == ast::ExprKind::MemberAccess) {
    arr_ty = exprTypeOf(*stmt.iterable);
    if (arr_ty.empty()) {
      arr_ty = inferTypeFromExpr(*stmt.iterable);
    }
    if (arr_ty.empty()) {
      error("cannot infer type of for-in iterable");
      return false;
    }
  } else {
    error("for-in iterable must be a local Array, a field, or a lo..hi range");
    return false;
  }
  ensureTypeAvailable(arr_ty);
  const ClassInfo* info = findClass(arr_ty);
  if (!info || info->type_args.empty() || info->template_name != "Array") {
    error("for-in requires Array<T>, got " + arr_ty);
    return false;
  }
  auto len_fit = info->field_index.find("length_");
  auto get_it = info->methods.find("get");
  if (len_fit == info->field_index.end() || get_it == info->methods.end()) {
    error("Array missing length_ / get for for-in lowering");
    return false;
  }
  if (named_values_.count(stmt.item_name) != 0) {
    error("for-in loop variable shadows existing name: " + stmt.item_name);
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
    error("String type not available");
    return nullptr;
  }
  if (expr.kind() == ast::ExprKind::StringLiteral) {
    auto init_it = info->methods.find("init");
    if (init_it == info->methods.end()) {
      error("String.init not found");
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
  error("expected String object");
  return nullptr;
}

llvm::Value* Codegen::emitStringByValue(const ast::Expr& expr) {
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error("String type not available");
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
    error("String type not available for i32→String");
    return nullptr;
  }
  llvm::Function* to_str = requireExtern("l_int_to_str");
  auto init_it = info->methods.find("init");
  if (!to_str || init_it == info->methods.end()) {
    error("l_int_to_str / String.init required for interpolation");
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
    llvm::Value* v = toI32(emitExpr(expr));
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
    llvm::Function* to_str = requireExtern(extern_name);
    ensureTypeAvailable("String");
    const ClassInfo* info = findClass("String");
    if (!to_str || !info) {
      error(std::string(extern_name) + " / String required for concatenation");
      return nullptr;
    }
    auto init_it = info->methods.find("init");
    if (init_it == info->methods.end()) {
      error("String.init required for concatenation");
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
  error("cannot convert expression to String for concatenation");
  return nullptr;
}

llvm::Value* Codegen::emitStringAdd(const ast::BinaryExpr& bin) {
  // a + b  ⇒  a.concat(b)  for String / string-literal / i32 (auto from) operands.
  // concat(String other) is pass-by-ref → pass String* for both this and other.
  // Result is heap-allocated so `t := a + b` can auto-free the owned String*.
  ensureTypeAvailable("String");
  const ClassInfo* info = findClass("String");
  if (!info) {
    error("String type not available for operator +");
    return nullptr;
  }
  auto mit = info->methods.find("concat");
  if (mit == info->methods.end()) {
    error("String.concat not found for operator + lowering");
    return nullptr;
  }
  llvm::Value* lhs = emitAsStringObjectPtr(*bin.left);
  llvm::Value* rhs = emitAsStringObjectPtr(*bin.right);
  if (!lhs || !rhs) {
    return nullptr;
  }
  llvm::Value* by_val = builder_.CreateCall(mit->second, {lhs, rhs}, "str.add");
  ensureDataLayout();
  llvm::Function* alloc = requireExtern("l_alloc");
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
    error("Array missing length_ / get for each inlining");
    return nullptr;
  }
  if (info.type_args.empty()) {
    error("Array.each inline requires a specialized Array<T>");
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
    error("each inline: lambda did not lower to a function");
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
  llvm::Value* arr_ptr = classObjectPtr(arr_name);
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
  arr_ptr = classObjectPtr(arr_name);
  idx = builder_.CreateLoad(i32Ty(), idx_slot, "each.i.body");
  llvm::Value* elem = builder_.CreateCall(get_it->second, {arr_ptr, idx}, "each.elem");

  std::vector<llvm::Value*> call_args;
  if (ctx) {
    call_args.push_back(ctx);
  }
  call_args.push_back(elem);
  if (lam_fn->arg_size() != call_args.size()) {
    error("each inline: lambda arity mismatch");
    return nullptr;
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
  ms_v = toI32(ms_v);
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
  llvm::Function* fn = requireExtern("l_arena_new");
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
    error("unknown arena: " + arena_name);
    return nullptr;
  }
  llvm::Value* arena_ptr = builder_.CreateLoad(ptrTy(), vit->second, arena_name + ".ptr");

  if (call.method == "alloc") {
    if (call.args.size() != 1 || !call.args[0].value) {
      error(call.loc, "arena.alloc(size) expects one size argument");
      return nullptr;
    }
    llvm::Function* alloc_fn = requireExtern("l_arena_alloc");
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
      error("String type not available for arena.new_string");
      return nullptr;
    }
    llvm::Function* alloc_fn = requireExtern("l_arena_alloc");
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
      error("String.init not found");
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
      error("unknown class in arena.new: " + neu.class_name);
      return nullptr;
    }
    const ClassInfo* info = findClass(neu.class_name);
    if (!info) {
      error("unknown class in arena.new: " + neu.class_name);
      return nullptr;
    }
    llvm::Function* alloc_fn = requireExtern("l_arena_alloc");
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

llvm::Value* Codegen::emitMethodCall(const ast::MethodCallExpr& call) {
  if (!call.object) {
    error("method call missing object");
    return nullptr;
  }

  // MemberAccess / other receivers: e.g. `c.Engine.start()` (explicit delegate).
  if (call.object->kind() != ast::ExprKind::Identifier) {
    std::string recv_ty = exprTypeOf(*call.object);
    if (recv_ty.empty()) {
      recv_ty = inferTypeFromExpr(*call.object);
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

      llvm::Value* fat = emitExpr(*call.object);
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
      error("method call object must be a class or interface value");
      return nullptr;
    }
    const ClassInfo* info = findClass(recv_ty);
    if (!info) {
      error("unknown class in method call: " + recv_ty);
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
        llvm::Value* outer = classValuePtr(*call.object, recv_ty);
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
            error("argument count mismatch calling " + recv_ty + "." + call.method);
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
          error("argument count mismatch calling " + recv_ty + "." + call.method);
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
      error("unknown method " + recv_ty + "." + call.method);
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
      llvm::Value* this_ptr = classValuePtr(*call.object, recv_ty);
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
      error("argument count mismatch calling " + recv_ty + "." + call.method);
      return nullptr;
    }
    if (callee_fn->getReturnType()->isVoidTy()) {
      builder_.CreateCall(callee_fn, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(callee_fn, call_args, is_static ? "static.call" : "mcall");
  }

  const auto& id = static_cast<const ast::IdentifierExpr&>(*call.object);
  auto lit = local_types_.find(id.name);
  auto vit = named_values_.find(id.name);

  // Instance field receiver: `this.field.method(...)`. Fields are not locals, so
  // resolve them against the enclosing class before falling through to the
  // class-name / static / free-function path below.
  if ((lit == local_types_.end() || vit == named_values_.end()) &&
      !current_class_.empty() && current_this_ && !current_method_is_static_) {
    const ClassInfo* owner = findClass(current_class_);
    if (owner) {
      auto fit = owner->field_index.find(physicalFieldName(id.name));
      if (fit != owner->field_index.end()) {
        const size_t fi = fit->second;
        std::string field_ty;
        if (fi < owner->field_types.size()) {
          field_ty = resolveTypeName(owner->field_types[fi]);
        }
        if (!field_ty.empty() && isClassType(field_ty)) {
          const ClassInfo* finfo = findClass(field_ty);
          llvm::Function* target = nullptr;
          const ClassInfo* callee_info = finfo;
          if (finfo) {
            auto own = finfo->methods.find(call.method);
            if (own != finfo->methods.end()) {
              target = own->second;
            }
          }
          if (target) {
            llvm::Value* field_ptr =
                builder_.CreateStructGEP(owner->type, current_this_, fi, id.name + ".slot");
            // Pointer-typed field slots hold the instance address; inline struct
            // fields are already addressed by the GEP.
            llvm::Type* slot_ty = owner->type->getElementType(fi);
            if (slot_ty->isPointerTy()) {
              field_ptr =
                  builder_.CreateLoad(slot_ty, field_ptr, id.name + ".self");
            }
            const ast::FuncDeclStmt* method_ast = nullptr;
            if (callee_info->ast) {
              for (const auto& m : callee_info->ast->methods) {
                if (m && m->name == call.method) {
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
              call_args.push_back(field_ptr);
              ai = 1;
            }
            for (const ast::Expr* arg : ordered) {
              llvm::Type* expected =
                  target->getFunctionType()->getParamType(ai);
              std::string expected_name;
              const size_t param_i =
                  is_static ? static_cast<size_t>(ai) : static_cast<size_t>(ai - 1);
              if (method_ast && param_i < method_ast->params.size()) {
                expected_name =
                    instantiateMemberType(method_ast->params[param_i].type_name,
                                          *callee_info);
              }
              llvm::Value* v = emitValueForParam(*arg, expected, expected_name);
              if (!v) {
                return nullptr;
              }
              call_args.push_back(v);
              ++ai;
            }
            if (target->arg_size() != call_args.size()) {
              error("argument count mismatch calling " + field_ty + "." + call.method);
              return nullptr;
            }
            if (target->getReturnType()->isVoidTy()) {
              builder_.CreateCall(target, call_args);
              return llvm::ConstantInt::get(i32Ty(), 0);
            }
            return builder_.CreateCall(target, call_args, "field.mcall");
          }
        }
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
          error("missing static method IR: " + id.name + "." + call.method);
          return nullptr;
        }
        llvm::Function* callee = mit->second;
        std::vector<const ast::Expr*> ordered;
        if (!resolveCallArgs(*static_ast, call.args, ordered)) {
          return nullptr;
        }
        if (callee->arg_size() != ordered.size()) {
          error("argument count mismatch calling " + id.name + "." + call.method);
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
        error("unknown object in method call: " + id.name);
        return nullptr;
      }
      // Fall through as a free-function call under package/module syntax.
    } else {
      if (fit == concrete_funcs_.end() || !fit->second) {
        error("unknown associated function " + id.name + "." + call.method);
        return nullptr;
      }
    }
    const ast::FuncDeclStmt* fn_ast = fit->second;
    // Class-name dispatch: `Variant.from_i32` / `File.read_all` — return type may differ
    // from the class (factories vs static helpers).
    const std::string ir_name = resolveFuncIrName(fn_ast->name);
    if (ir_name.empty()) {
      error("unknown associated function " + id.name + "." + call.method);
      return nullptr;
    }
    llvm::Function* callee = module_->getFunction(ir_name);
    if (!callee) {
      error("missing associated function IR: " + ir_name);
      return nullptr;
    }
    std::vector<const ast::Expr*> ordered;
    if (!resolveCallArgs(*fn_ast, call.args, ordered)) {
      return nullptr;
    }
    if (callee->arg_size() != ordered.size()) {
      error("argument count mismatch calling " + id.name + "." + call.method);
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
      error("unknown class in method call: " + lit->second);
      return nullptr;
    }

    // [closure-integration]: Array.each + inline Lambda → while desugar (capturing-safe).
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
      // [delegate]: promote from delegates; multiple hits → hard ambiguity error.
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
        llvm::Value* outer = classObjectPtr(id.name);
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
      error("unknown method " + lit->second + "." + call.method);
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
          error("keyword arguments require a known method signature");
          return nullptr;
        }
        if (!a.value) {
          error("null argument in method call");
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
        this_ptr = classObjectPtr(id.name);
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
      error("argument count mismatch calling " + lit->second + "." + call.method);
      return nullptr;
    }
    if (callee_fn->getReturnType()->isVoidTy()) {
      builder_.CreateCall(callee_fn, call_args);
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    return builder_.CreateCall(callee_fn, call_args, is_static ? "static.call" : "mcall");
  }

  std::vector<llvm::Value*> args;
  for (const ast::CallArg& arg : call.args) {
    if (!arg.value) {
      return nullptr;
    }
    if (!arg.name.empty()) {
      error("keyword arguments not supported on interface dispatch yet");
      return nullptr;
    }
    llvm::Value* v = toI32(emitExpr(*arg.value));
    if (!v) {
      return nullptr;
    }
    args.push_back(v);
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
  std::unordered_map<std::string, llvm::AllocaInst*> outer_slots;
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
        fields.push_back(outer_slots[name]->getAllocatedType());
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
      // [closure-escape]: escaping closure — heap-allocate ctx and copy capture values into it.
      llvm::Function* alloc_fn = requireExtern("l_alloc");
      if (!alloc_fn) {
        return nullptr;
      }
      ensureDataLayout();
      const uint64_t ctx_bytes = module_->getDataLayout().getTypeAllocSize(ctx_ty);
      llvm::Value* ctx_sz =
          llvm::ConstantInt::get(i64Ty(), static_cast<uint64_t>(ctx_bytes), /*isSigned=*/false);
      llvm::Value* ctx_heap = builder_.CreateCall(alloc_fn, {ctx_sz}, "lambda.ctx.heap");
      for (unsigned i = 0; i < caps.size(); ++i) {
        llvm::AllocaInst* slot = outer_slots[caps[i]];
        llvm::Value* val =
            builder_.CreateLoad(slot->getAllocatedType(), slot, caps[i] + ".esc.ld");
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
    case ast::ExprKind::IntLiteral: {
      const auto& lit = static_cast<const ast::IntLiteralExpr&>(expr);
      long long n = 0;
      try {
        n = std::stoll(lit.value);
      } catch (...) {
        error("invalid integer literal: " + lit.value);
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
        error("invalid float literal: " + lit.value);
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
        return makeStringLitObject(file, "ctx.file");
      }
      if (id.name == "__FUNC__") {
        const std::string& fn =
            current_fn_name_.empty() ? std::string("<unknown>") : current_fn_name_;
        return makeStringLitObject(fn, "ctx.func");
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
        error("cannot use 'this' inside a static method");
        return nullptr;
      }
      // Method body: field load via this (logical → physical).
      if (!current_class_.empty()) {
        const ClassInfo* info = findClass(current_class_);
        if (info) {
          auto fit = info->field_index.find(physicalFieldName(id.name));
          if (fit != info->field_index.end()) {
            if (current_method_is_static_ || !current_this_) {
              error("cannot access instance field '" + id.name + "' from a static method");
              return nullptr;
            }
            llvm::Value* gep =
                builder_.CreateStructGEP(info->type, current_this_, fit->second, id.name);
            llvm::Type* fty = info->type->getElementType(fit->second);
            return builder_.CreateLoad(fty, gep, id.name);
          }
        }
      }

      auto it = named_values_.find(id.name);
      if (it != named_values_.end()) {
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

      error("unknown variable: " + id.name);
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
      if (!access.object) {
        error("member access missing object");
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
          error("unknown class in member access: " + obj_ty);
          return nullptr;
        }
        const PropResolve pr = resolveProperty(*info, access.member, PropAccess::Read);
        if (pr.kind == PropResolveKind::Method) {
          auto mit = info->methods.find(pr.method_name);
          if (mit == info->methods.end()) {
            error("missing getter " + pr.method_name);
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
          error(access.loc, "unknown property '" + access.member + "' (no get_" +
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
    case ast::ExprKind::MethodCall:
      return emitMethodCall(static_cast<const ast::MethodCallExpr&>(expr));
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
        error("cannot load index expression: unknown element type");
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
        error("unknown class in NewExpr: " + neu.class_name);
        return nullptr;
      }
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
    case ast::ExprKind::Coalesce: {
      // `opt ?? fallback` — evaluate `opt` ONCE into a temporary, branch on
      // `is_some()`, then select `unwrap()` or the fallback. The temporary is
      // registered as a local so method dispatch can take its address.
      const auto& co = static_cast<const ast::CoalesceExpr&>(expr);
      if (!co.left || !co.right) {
        error("?? requires both operands");
        return nullptr;
      }

      const std::string left_ty = exprTypeOf(*co.left);
      if (left_ty.rfind("Optional<", 0) != 0) {
        error("left operand of '??' must be Optional<T>, got '" + left_ty + "'");
        return nullptr;
      }
      if (!ensureTypeAvailable(left_ty)) {
        return nullptr;
      }
      const ClassInfo* opt_info = findClass(left_ty);
      if (!opt_info || !opt_info->type) {
        error("unknown Optional type in '??': " + left_ty);
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
        error("'??' left operand has unexpected representation in " + left_ty);
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
      if (payload_is_class && co.right->kind() == ast::ExprKind::StringLiteral) {
        none_val = emitStringObjectPtr(*co.right);
      } else {
        none_val = emitExpr(*co.right);
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
        error("'??' arms have mismatched types: some=" + so.str() + " none=" + no.str());
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

      if (bin.op == "+" || bin.op == "-" || bin.op == "*" || bin.op == "/") {
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
          error(bin.loc, "incompatible types for '" + bin.op + "': '" + lt + "' and '" + rt +
                             "'");
          return nullptr;
        }
        lhs = emitNumericCast(lhs, left_ty, promoted);
        rhs = emitNumericCast(rhs, right_ty, promoted);
        if (!lhs || !rhs) {
          return nullptr;
        }
        if (promoted == "f64") {
          if (bin.op == "+") return builder_.CreateFAdd(lhs, rhs, "faddtmp");
          if (bin.op == "-") return builder_.CreateFSub(lhs, rhs, "fsubtmp");
          if (bin.op == "*") return builder_.CreateFMul(lhs, rhs, "fmultmp");
          return builder_.CreateFDiv(lhs, rhs, "fdivtmp");
        }
        if (bin.op == "+") return builder_.CreateAdd(lhs, rhs, "addtmp");
        if (bin.op == "-") return builder_.CreateSub(lhs, rhs, "subtmp");
        if (bin.op == "*") return builder_.CreateMul(lhs, rhs, "multmp");
        return builder_.CreateSDiv(lhs, rhs, "divtmp");
      }

      // C-string / String equality via l_str_eq (e.g. get_build_mode() == "Debug").
      // Trigger when either side is a string literal / String (not bare ptr==ptr).
      if ((bin.op == "==" || bin.op == "!=") &&
          (isStringOperand(*bin.left) || isStringOperand(*bin.right) || lt == "String" ||
           rt == "String") &&
          !isNumericType(lt) && !isNumericType(rt)) {
        auto toCStr = [this](llvm::Value* v, const std::string& ty,
                             const ast::Expr& e) -> llvm::Value* {
          if (ty == "String" || (ty.empty() && exprTypeOf(e) == "String")) {
            ensureTypeAvailable("String");
            const ClassInfo* info = findClass("String");
            if (!info || !v) {
              return nullptr;
            }
            auto fit = info->field_index.find("data_");
            if (fit == info->field_index.end()) {
              return nullptr;
            }
            llvm::Value* gep =
                builder_.CreateStructGEP(info->type, v, fit->second, "streq.data");
            return builder_.CreateLoad(ptrTy(), gep, "streq.cstr");
          }
          return v;
        };
        llvm::Function* str_eq = requireExtern("l_str_eq");
        if (!str_eq) {
          return nullptr;
        }
        llvm::Value* a = toCStr(lhs, lt, *bin.left);
        llvm::Value* b = toCStr(rhs, rt, *bin.right);
        if (!a || !b) {
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

      // Comparisons: promote numeric operands to a common type.
      if (isNumericType(lt) && isNumericType(rt)) {
        const std::string promoted = promoteNumericTypes(lt, rt);
        lhs = emitNumericCast(lhs, lt, promoted);
        rhs = emitNumericCast(rhs, rt, promoted);
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
        lhs = toI32(lhs);
        rhs = toI32(rhs);
        if (bin.op == "==") return builder_.CreateICmpEQ(lhs, rhs, "eqtmp");
        if (bin.op == "!=") return builder_.CreateICmpNE(lhs, rhs, "netmp");
        if (bin.op == "<") return builder_.CreateICmpSLT(lhs, rhs, "lttmp");
        if (bin.op == ">") return builder_.CreateICmpSGT(lhs, rhs, "gttmp");
        if (bin.op == "<=") return builder_.CreateICmpSLE(lhs, rhs, "letmp");
        if (bin.op == ">=") return builder_.CreateICmpSGE(lhs, rhs, "getmp");
      }

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
  llvm::Function* alloc = requireExtern("l_alloc");
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
      error("null argument calling " + fn.name);
      return false;
    }
    if (a.name.empty()) {
      if (next_pos >= fn.params.size()) {
        error("too many arguments calling " + fn.name);
        return false;
      }
      if (ordered[next_pos] != nullptr) {
        error("duplicate argument for parameter '" + fn.params[next_pos].name + "'");
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
      error("unknown keyword argument '" + a.name + "' for " + fn.name);
      return false;
    }
    if (ordered[idx] != nullptr) {
      error("duplicate argument for parameter '" + a.name + "'");
      return false;
    }
    ordered[idx] = a.value.get();
  }
  for (size_t i = 0; i < ordered.size(); ++i) {
    if (ordered[i] == nullptr) {
      error("missing argument '" + fn.params[i].name + "' calling " + fn.name);
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

llvm::Value* Codegen::emitValueForParam(const ast::Expr& expr, llvm::Type* expected,
                                        const std::string& expected_type_name) {
  // Class formals are ABI pointers: coerce string literals → temporary String*.
  if (expected_type_name == "String" && expr.kind() == ast::ExprKind::StringLiteral) {
    return emitStringObjectPtr(expr);
  }

  // Trailing block / lambda / bare function name → function pointer matching fun(...).
  if (!expected_type_name.empty() && isFunTypeName(expected_type_name)) {
    if (expr.kind() == ast::ExprKind::Lambda) {
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

  if (!expected_type_name.empty() && isClassType(expected_type_name)) {
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
      error("class argument type mismatch for " + expected_type_name);
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
        llvm::Value* obj = classObjectPtr(id.name);
        if (!obj) {
          return nullptr;
        }
        return boxToInterface(obj, lit->second, expected_type_name);
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
      return boxToInterface(obj, neu.class_name, expected_type_name);
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
    v = toI32(v);
  }
  return v;
}

llvm::Value* Codegen::emitGenericOrPlainCall(const ast::CallExpr& call) {
  auto requirePositional = [&](size_t n, const char* name) -> bool {
    if (call.args.size() != n) {
      error(std::string(name) + " expects exactly " + std::to_string(n) + " argument(s)");
      return false;
    }
    for (const ast::CallArg& a : call.args) {
      if (!a.name.empty()) {
        error(std::string(name) + " does not accept keyword arguments");
        return false;
      }
      if (!a.value) {
        error("null argument to " + std::string(name));
        return false;
      }
    }
    return true;
  };

  // [uid-counter]: never emit a runtime call for uid_counter (semantic should have rejected).
  if (call.callee == "uid_counter") {
    error(call.loc, "uid_counter() is only allowed in compile-time context");
    return nullptr;
  }
  if (call.callee == "compile_error" || call.callee == "comptime_assert") {
    error(call.loc, call.callee + "() is only allowed in compile-time context");
    return nullptr;
  }

  // [closure-integration] `delay(ms) { ... }` / `delay(ms, || ...)` with inline Lambda → spawn desugar.
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
        error("argument count mismatch calling closure '" + call.callee + "'");
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
          error("null argument to closure '" + call.callee + "'");
          return nullptr;
        }
        if (!call.args[i].name.empty()) {
          error("keyword arguments not supported on closures");
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
      error("fat closure '" + call.callee + "' missing local state");
      return nullptr;
    }
    std::vector<std::string> param_tys;
    std::string ret;
    bool has_ret = false;
    if (!parseFunTypeName(resolveTypeName(lit->second), param_tys, ret, has_ret)) {
      error("invalid fat-closure payload type for '" + call.callee + "'");
      return nullptr;
    }
    if (param_tys.size() != call.args.size()) {
      error("argument count mismatch calling fat closure '" + call.callee + "'");
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
        error("null argument to fat closure '" + call.callee + "'");
        return nullptr;
      }
      if (!call.args[i].name.empty()) {
        error("keyword arguments not supported on fat closures");
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
      llvm::FunctionType* fty = llvmFunctionTypeForFunName(ty_it->second);
      if (!fty) {
        return nullptr;
      }
      if (fty->getNumParams() != call.args.size()) {
        error("argument count mismatch calling captured function '" + call.callee + "'");
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
          error("null argument to captured function '" + call.callee + "'");
          return nullptr;
        }
        if (!call.args[i].name.empty()) {
          error("keyword arguments not supported on captured functions");
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
      llvm::FunctionType* fty = llvmFunctionTypeForFunName(lit->second);
      if (!fty) {
        return nullptr;
      }
      if (fty->getNumParams() != call.args.size()) {
        error("argument count mismatch calling function value '" + call.callee + "'");
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
          error("null argument to function value '" + call.callee + "'");
          return nullptr;
        }
        if (!call.args[i].name.empty()) {
          error("keyword arguments not supported on function values");
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
              error("keyword arguments require a known method signature");
              return nullptr;
            }
            if (!a.value) {
              error("null argument in sibling method call");
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
          error("argument count mismatch calling " + current_class_ + "." + call.callee);
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
      llvm::Function* print_str = requireExtern("l_print_str");
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
          error("String type not available for print");
          return nullptr;
        }
        auto fit = info->field_index.find("data_");
        if (fit == info->field_index.end()) {
          error("String.data_ not found");
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
    llvm::Value* arg = emitExpr(arg_expr);
    if (!arg) {
      return nullptr;
    }
    // Dispatch by L type / LLVM type → matching l_print_* C ABI helper.
    if (arg_ty == "bool" || arg->getType()->isIntegerTy(1)) {
      llvm::Function* print_b = requireExtern("l_print_bool");
      if (!print_b) {
        return nullptr;
      }
      llvm::Value* as_i32 = builder_.CreateZExt(toI1(arg), i32Ty(), "print.bool");
      builder_.CreateCall(print_b, {as_i32});
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    if (arg_ty == "f64" || arg->getType()->isDoubleTy()) {
      llvm::Function* print_f = requireExtern("l_print_f64");
      if (!print_f) {
        return nullptr;
      }
      if (!arg->getType()->isDoubleTy()) {
        arg = emitNumericCast(arg, arg_ty.empty() ? "i32" : arg_ty, "f64");
        if (!arg) {
          return nullptr;
        }
      }
      builder_.CreateCall(print_f, {arg});
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    if (arg_ty == "i64" || arg->getType()->isIntegerTy(64)) {
      llvm::Function* print_i64 = requireExtern("l_print_i64");
      if (!print_i64) {
        return nullptr;
      }
      if (!arg->getType()->isIntegerTy(64)) {
        arg = emitNumericCast(arg, arg_ty.empty() ? "i32" : arg_ty, "i64");
        if (!arg) {
          return nullptr;
        }
      }
      builder_.CreateCall(print_i64, {arg});
      return llvm::ConstantInt::get(i32Ty(), 0);
    }
    llvm::Function* print_int = requireExtern("l_print_int");
    if (!print_int) {
      return nullptr;
    }
    arg = toI32(arg);
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
          obj = classObjectPtr(id.name);
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
        call_args.push_back(toI32(v));
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
    error("unknown function: " + call.callee);
    return nullptr;
  }
  llvm::Function* callee = module_->getFunction(ir_name);
  if (!callee) {
    error("unknown function: " + call.callee);
    return nullptr;
  }

  std::vector<const ast::Expr*> ordered;
  if (fn_ast) {
    if (!resolveCallArgs(*fn_ast, call.args, ordered)) {
      return nullptr;
    }
  } else {
    if (callee->arg_size() != call.args.size()) {
      error("argument count mismatch calling " + call.callee);
      return nullptr;
    }
    for (const ast::CallArg& a : call.args) {
      if (!a.name.empty()) {
        error("keyword arguments require a known function signature for " + call.callee);
        return nullptr;
      }
      if (!a.value) {
        error("null argument calling " + call.callee);
        return nullptr;
      }
      ordered.push_back(a.value.get());
    }
  }

  if (callee->arg_size() != ordered.size()) {
    error("argument count mismatch calling " + call.callee);
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
  if (callee->getReturnType()->isVoidTy()) {
    builder_.CreateCall(callee, args);
    return llvm::ConstantInt::get(i32Ty(), 0);
  }
  return builder_.CreateCall(callee, args, "calltmp");
}

}  // namespace l::codegen
