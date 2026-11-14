#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ast/ast.hpp"
#include "diagnostic/diagnostic.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

namespace llvm {
class TargetMachine;
}

namespace l::codegen {

llvm::LLVMContext& globalContext();

class Codegen {
public:
  explicit Codegen(std::string module_name, diag::DiagnosticEngine* diags = nullptr);

  bool emit(const ast::Module& module);
  bool emitObjectFile(const std::string& object_path);

  // 0 = -O0 (default), 1..3 map to LLVM OptimizationLevel::O1..O3.
  void setOptLevel(unsigned level) { opt_level_ = level > 3 ? 3 : level; }
  unsigned optLevel() const { return opt_level_; }

  // `--shared`: export `pub` functions (DLLExport on Windows) and emit PIC objects.
  void setSharedMode(bool shared) { shared_mode_ = shared; }
  bool sharedMode() const { return shared_mode_; }

  llvm::Module& module() { return *module_; }
  const llvm::Module& module() const { return *module_; }

private:
  struct InterfaceInfo {
    std::vector<std::string> method_names;
    // Method slots start at 1; slot 0 is reserved for TypeID ([vtable-layout]).
    std::unordered_map<std::string, unsigned> method_index;
    std::unordered_map<std::string, const ast::FuncDeclStmt*> methods;
  };

  struct ClassInfo {
    llvm::StructType* type = nullptr;
    std::unordered_map<std::string, unsigned> field_index;
    std::vector<std::string> field_types;
    std::unordered_map<std::string, bool> field_is_pub;  // keyed by physical name (age_)
    std::vector<std::string> interfaces;
    std::unordered_map<std::string, llvm::Function*> methods;
    std::unordered_map<std::string, llvm::GlobalVariable*> vtables;
    // Unique identity stored at vtable[0] for safe interface downcasts.
    llvm::GlobalVariable* type_id = nullptr;
    const ast::ClassDeclStmt* ast = nullptr;
    // For specialized generic classes (e.g. Array<i32>): concrete type args.
    std::vector<std::string> type_args;
    std::string template_name;  // "Array" when name is "Array<i32>"
    std::string ir_name;        // LLVM-friendly name (Array_i32)
  };

  // Property fallback ([property]): `u.age` → get_age/set_age else physical age_.
  enum class PropAccess { Read, Write };
  enum class PropResolveKind { Method, Field, None };
  struct PropResolve {
    PropResolveKind kind = PropResolveKind::None;
    std::string method_name;
    std::string field_name;
  };
  PropResolve resolveProperty(const ClassInfo& info, const std::string& prop,
                              PropAccess access) const;

  llvm::Type* i32Ty();
  llvm::Type* i64Ty();
  llvm::Type* f64Ty();
  llvm::Type* i1Ty();
  llvm::Type* voidTy();
  llvm::Type* ptrTy();
  // Numeric promotion helpers (i32 < i64 < f64).
  static int numericRank(const std::string& ty);
  static bool isNumericType(const std::string& ty);
  static std::string promoteNumericTypes(const std::string& a, const std::string& b);
  llvm::Value* emitNumericCast(llvm::Value* v, const std::string& from_ty,
                               const std::string& to_ty);
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
  // Look up a C ABI symbol previously declared via `extern module` (no auto-create).
  llvm::Function* requireExtern(const std::string& name);
  bool declareExternFunc(const ast::FuncDeclStmt& fn);
  llvm::Type* llvmTypeForTypeName(const std::string& name);
  // ABI type for a formal parameter: class → ptr, interface → fat ptr, else value.
  llvm::Type* abiTypeForParam(const std::string& type_name);
  // Bind one formal into named_values_ / local_types_ / heap_locals_ / readonly_refs_.
  void bindParam(llvm::Function* fn, llvm::Value* arg, const ast::Param& param,
                 const std::string& resolved_type);
  bool rejectReadonlyMutation(const ast::Expr& target, const ast::SourceLocation& loc);
  // Best-effort expression type for operator lowering / := inference.
  std::string exprTypeOf(const ast::Expr& expr) const;
  bool isStringOperand(const ast::Expr& expr) const;
  // `+` that should lower to String concat (literals / String / i32 coercion).
  bool isStringConcatBinary(const ast::BinaryExpr& bin) const;
  // True when init yields an owning heap class pointer (auto-free candidate).
  bool isOwnedHeapClassExpr(const ast::Expr& expr) const;
  // True when init is `arena.new` / `arena.new_string` (bump-owned, no l_free).
  bool isArenaOwnedClassExpr(const ast::Expr& expr) const;
  // Free Array.data_ (+ interface element heaps) when an Array local leaves scope.
  bool emitArrayOwnedCleanup(const std::string& name, const ClassInfo& info);
  llvm::Value* emitArenaBuiltin(const ast::CallExpr& call);
  llvm::Value* emitArenaMethod(const std::string& arena_name, const ast::MethodCallExpr& call);
  // Materialize a String object pointer (alloca / class ptr) for concat / print.
  llvm::Value* emitStringObjectPtr(const ast::Expr& expr);
  // Coerce String / string-lit / i32 → String* for concatenation.
  llvm::Value* emitAsStringObjectPtr(const ast::Expr& expr);
  llvm::Value* emitIntToStringObject(llvm::Value* i32_val);
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
  static bool isFunTypeName(const std::string& name);
  // Parse `fun(A,B)` / `fun(A)->R` into param type names + return type.
  static bool parseFunTypeName(const std::string& name, std::vector<std::string>& params,
                               std::string& ret, bool& has_explicit_ret);
  llvm::FunctionType* llvmFunctionTypeForFunName(const std::string& fun_type_name);
  llvm::Value* emitLambda(const ast::LambdaExpr& expr, const std::string& fun_type_name);
  // Stack locals actually captured (filters globals / consts out of LambdaExpr::captures).
  std::vector<std::string> resolveCaptureLocals(const ast::LambdaExpr& expr) const;
  llvm::Value* emitArrayLiteral(const ast::ArrayLiteralExpr& expr);
  llvm::Value* emitCast(const ast::CastExpr& expr);
  llvm::Value* emitIncDec(const ast::IncDecExpr& expr);
  llvm::Value* emitTry(const ast::TryExpr& expr);
  // Pointer to a class object (local / temp / method result spilled).
  llvm::Value* classValuePtr(const ast::Expr& expr, const std::string& class_name);
  bool ensureTypeAvailable(const std::string& name);
  bool specializeClass(const ast::ClassDeclStmt& tmpl, const std::vector<std::string>& args,
                       const std::string& instance_name);
  uint64_t allocSizeOfTypeName(const std::string& type_name);
  llvm::Type* elementTypeForPtrIndex() const;
  // Resolve elem type for `base[i]` (Map keys_/occupied_/values_, Array data_, …).
  llvm::Type* elementTypeForIndexedBase(const ast::Expr& object) const;
  // Substitute class template args into a method return type (e.g. Optional<V>).
  std::string instantiateMemberType(const std::string& type_name,
                                    const ClassInfo& info) const;
  llvm::Value* emitIndexLValue(const ast::IndexExpr& expr);
  llvm::Value* emitSizeof(const ast::SizeofExpr& expr);
  llvm::Value* emitAddrOf(const ast::AddrOfExpr& expr);

  llvm::Value* toI1(llvm::Value* v);
  llvm::Value* toI32(llvm::Value* v);
  bool emitStmtList(const std::vector<ast::StmtPtr>& stmts);
  bool emitStmtListScoped(const std::vector<ast::StmtPtr>& stmts);
  void branchTo(llvm::BasicBlock* bb);

  void enterDeferScope();
  void exitDeferScope();
  bool flushCurrentScopeDefers();
  bool flushAllDefers();
  // Auto-free for `heap` locals without an explicit `defer l_free(...)`.
  void noteAutoFreeLocal(const std::string& name);
  void cancelAutoFreeLocal(const std::string& name);
  // If a returned expression aliases a heap local (e.g. `return c as ptr`), drop auto-free.
  void cancelAutoFreeEscaping(const ast::Expr& expr);
  void noteExplicitLFreeInDefer(const ast::DeferStmt& stmt);
  bool emitLFreeOfLocal(const std::string& name);
  bool flushCurrentScopeAutoFrees();
  bool flushAllAutoFrees();
  bool finishCleanups();  // defers then auto-frees

  bool emitStmt(const ast::Stmt& stmt);
  llvm::Value* emitExpr(const ast::Expr& expr);
  llvm::Value* emitLValue(const ast::Expr& expr);
  llvm::Value* emitHeapExpr(const ast::HeapExpr& expr);
  // Run LLVM default optimization pipeline before object emission.
  void optimizeModule(llvm::TargetMachine* tm);

  bool registerInterface(const ast::InterfaceDeclStmt& decl);
  bool emitClassStruct(const ast::ClassDeclStmt& decl);
  bool declareClassMethods(const ast::ClassDeclStmt& decl);
  bool buildVtables(const ast::ClassDeclStmt& decl);
  bool emitClassMethodBodies(const ast::ClassDeclStmt& decl);
  bool emitMethodBody(const std::string& class_name, const ast::FuncDeclStmt& method);
  // Phase-1 auto-clone: bitwise / value copy of the object (primitives + pure data).
  bool emitAutoCloneBody(const std::string& class_name, llvm::Function* fn);
  // Compiler-generated `to_string() -> String` field dump.
  bool emitAutoToStringBody(const std::string& class_name, llvm::Function* fn);
  // Helpers for auto to_string string building (return String*).
  llvm::Value* makeStringLitObject(const std::string& text, const std::string& name);
  llvm::Value* concatStringObjects(llvm::Value* lhs, llvm::Value* rhs);

  bool emitFuncDecl(const ast::FuncDeclStmt& fn);
  bool emitLifecycleWrappers(const ast::Module& module);
  bool emitHookBodies(llvm::Function* fn, const std::vector<std::unique_ptr<ast::FuncDeclStmt>>& hooks);
  void emitLifecycleDeinitCall();
  bool emitSpecializedFunc(const ast::FuncDeclStmt& tmpl,
                           const std::string& mangled,
                           const std::unordered_map<std::string, std::string>& subst);
  llvm::Function* ensureSpecialized(const ast::FuncDeclStmt& tmpl,
                                    const std::vector<std::string>& arg_type_names,
                                    const ast::SourceLocation& call_loc);
  llvm::Value* emitGenericOrPlainCall(const ast::CallExpr& call);
  // Reorder positional+keyword CallArgs to match FuncDecl param order.
  bool resolveCallArgs(const ast::FuncDeclStmt& fn, const std::vector<ast::CallArg>& raw,
                       std::vector<const ast::Expr*>& ordered);
  llvm::Value* emitValueForParam(const ast::Expr& expr, llvm::Type* expected,
                                 const std::string& expected_type_name);
  // Bare function name / fun-local → LLVM function pointer (C calling convention).
  llvm::Value* emitNamedFunctionPointer(const std::string& name,
                                        const ast::SourceLocation& loc);

  bool emitVarDecl(const ast::VarDeclStmt& decl);
  // `(a, b) := classValue` — extract fields in declaration order into locals.
  bool emitDestructureDecl(const ast::DestructureDeclStmt& decl);
  // Module-level `const` / `constd` / typed globals → inline Constant or GlobalVariable.
  // Class globals are zero-init GlobalVariables; `Class()` runs in `__l_global_init`.
  bool emitGlobalVarDecl(const ast::VarDeclStmt& decl);
  bool emitPendingGlobalClassInits();
  llvm::Constant* evalConstInit(const ast::Expr& expr, llvm::Type* expected_ty);
  // Resolve local alloca or module GlobalVariable storage.
  llvm::Value* lookupStorage(const std::string& name);
  // Infer local type from RHS for `name := expr` (NewExpr / heap New / literals).
  std::string inferTypeFromExpr(const ast::Expr& expr);
  bool emitAssign(const ast::AssignStmt& assign);
  bool emitReturn(const ast::ReturnStmt& ret);
  bool emitExprStmt(const ast::ExprStmt& stmt);
  bool emitIf(const ast::IfStmt& stmt);
  bool emitMatch(const ast::MatchStmt& stmt);
  bool emitWhile(const ast::WhileStmt& stmt);
  bool emitForIn(const ast::ForInStmt& stmt);
  bool emitForInRange(const ast::ForInStmt& stmt, const ast::BinaryExpr& range);
  bool emitDefer(const ast::DeferStmt& stmt);
  bool emitSync(const ast::SyncBlockStmt& stmt);
  bool emitSpawn(const ast::SpawnBlockStmt& stmt);
  // Shared lowering for statement/expression spawn. Returns opaque join handle (`ptr`).
  llvm::Value* emitSpawnBody(const std::vector<ast::StmtPtr>& body,
                             const ast::SourceLocation& loc);
  // Locals referenced inside `spawn { }` that live in the parent frame.
  // `body_locals` names declared in the spawn body must not be captured from outer.
  void collectSpawnCaptures(const ast::Stmt& stmt, std::vector<std::string>& out,
                            std::unordered_set<std::string>& seen,
                            const std::unordered_set<std::string>* body_locals) const;
  void collectSpawnCaptures(const ast::Expr& expr, std::vector<std::string>& out,
                            std::unordered_set<std::string>& seen,
                            const std::unordered_set<std::string>* body_locals) const;
  bool flushHeldSyncLocks();
  // Inline `arr.each { |x| ... }` / `arr.each(|x| ...)` (capturing-safe).
  llvm::Value* emitArrayEachInline(const std::string& arr_name, const ClassInfo& info,
                                   const ast::LambdaExpr& lambda);
  // `delay(ms) { ... }` / `delay(ms, || ...)` → spawn { sleep(ms); body } (capturing-safe).
  llvm::Value* emitDelayInline(const ast::Expr& ms_expr, const ast::LambdaExpr& lambda,
                               const ast::SourceLocation& loc);
  // Names declared inside a spawn body (excluded from outer capture).
  void collectSpawnLocalDecls(const ast::Stmt& stmt,
                              std::unordered_set<std::string>& locals) const;

  llvm::Value* boxToInterface(llvm::Value* class_ptr, const std::string& class_name,
                              const std::string& iface_name);
  // Heap-allocate + init a NewExpr so Class→interface fat pointers remain valid when
  // stored in Array<IFace> / other heap containers (entry allocas would dangle).
  llvm::Value* heapAllocNewExpr(const ast::NewExpr& neu);
  // Escaped closure ABI: `{ fn_ptr, ctx_ptr }` (same layout as interface fat ptr).
  llvm::StructType* fatClosureTy();
  // Free heap ctx + capture cells for an escaped fat-closure local.
  bool emitFatClosureCleanup(const std::string& name);
  // Create (or return) the unique TypeID global for a class (vtable slot 0).
  llvm::GlobalVariable* ensureTypeId(ClassInfo& info);
  // VTable layout: [TypeID, method0, method1, ...] — length = 1 + method count.
  llvm::ArrayType* vtableArrayTy(const InterfaceInfo& iface);
  llvm::Value* emitMethodCall(const ast::MethodCallExpr& call);

  void error(const std::string& message);
  void error(const ast::SourceLocation& loc, const std::string& message);
  void ensureDataLayout();
  ast::SourceLocation defaultLoc() const;

  static std::string mangleName(const std::string& module_name, const std::string& name,
                                bool is_pub);
  std::string resolveFuncIrName(const std::string& callee) const;

  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> builder_;
  std::unordered_map<std::string, llvm::AllocaInst*> named_values_;
  std::unordered_map<std::string, std::string> local_types_;
  // Compile-time `const` — inlined, no GlobalVariable.
  std::unordered_map<std::string, llvm::Constant*> const_values_;
  std::unordered_map<std::string, std::string> const_types_;
  // `constd` / mutable module globals.
  std::unordered_map<std::string, llvm::GlobalVariable*> global_vars_;
  std::unordered_map<std::string, std::string> global_types_;
  std::unordered_set<std::string> global_readonly_;  // constd (and infer without !)
  // Class globals: runtime `init` / field ctor deferred into `__l_global_init`.
  struct PendingGlobalClassInit {
    std::string name;
    std::string class_name;
    const ast::NewExpr* neu = nullptr;
  };
  std::vector<PendingGlobalClassInit> pending_global_class_inits_;
  // Capturing lambda locals: call with (ctx, args...).
  struct ClosureLocal {
    llvm::Function* fn = nullptr;
    llvm::Value* ctx = nullptr;
    llvm::StructType* ctx_ty = nullptr;
  };
  std::unordered_map<std::string, ClosureLocal> closure_locals_;
  // Locals holding an escaped fat closure value `{fn_ptr, ctx_ptr}` ([closure-escape]).
  // `local_types_` still stores the payload fun(...) type for call ABI.
  std::unordered_set<std::string> fat_closure_locals_;
  // Heap cells owned by an escaped fat closure (ctx + promoted captures) — free on scope exit.
  std::unordered_map<std::string, llvm::StructType*> fat_closure_ctx_tys_;
  // While emitting a capturing lambda body: ctx arg + field index / type per capture.
  llvm::Value* current_lambda_ctx_ = nullptr;
  llvm::StructType* current_lambda_ctx_ty_ = nullptr;
  // Escaping closures store capture values in ctx (not ptr-to-stack); body does a single load.
  bool current_lambda_ctx_by_value_ = false;
  std::unordered_map<std::string, unsigned> capture_slots_;
  std::unordered_map<std::string, std::string> capture_types_;
  // Set by emitLambda when captures exist; consumed by emitVarDecl / emitReturn.
  llvm::Value* pending_closure_ctx_ = nullptr;
  llvm::StructType* pending_closure_ctx_ty_ = nullptr;
  std::unordered_set<std::string> heap_locals_;  // class locals stored as ptr alloca
  // Locals that auto-cleanup via l_arena_free (not l_free).
  std::unordered_set<std::string> arena_locals_;
  // Per-scope names of `heap`-allocated locals still needing auto l_free.
  std::vector<std::vector<std::string>> auto_free_scopes_;
  // Class params without trailing '!': reject field / index stores through them.
  std::unordered_set<std::string> readonly_refs_;
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
  // Currently held `sync` mutexes (global lock ptrs); unlocked on scope exit / return.
  std::vector<llvm::Value*> held_sync_locks_;

  std::string current_class_;
  llvm::Value* current_this_ = nullptr;
  bool current_method_is_static_ = false;
  // Enclosing function return type name (e.g. Result<i32,String>) for `expr?`.
  std::string current_fn_return_type_;
  // Logical source name for `__FUNC__` (not IR-mangled).
  std::string current_fn_name_;
  unsigned lambda_seq_ = 0;
  unsigned lambda_ctx_seq_ = 0;
  unsigned spawn_seq_ = 0;
  unsigned delay_seq_ = 0;
  // Injected into `main`: call at entry / before every return.
  llvm::Function* global_init_fn_ = nullptr;
  llvm::Function* global_deinit_fn_ = nullptr;
  bool inject_deinit_on_return_ = false;

  diag::DiagnosticEngine* diags_ = nullptr;
  bool had_error_ = false;
  unsigned opt_level_ = 0;
  bool shared_mode_ = false;
};

}  // namespace l::codegen
