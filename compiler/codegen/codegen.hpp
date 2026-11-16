#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ast/ast.hpp"
#include "diagnostic/diagnostic.hpp"

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
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
  // `-g`: close the compile unit so it is attached to the module. Idempotent;
  // emitObjectFile() calls it too, so `--emit-llvm` must call it explicitly.
  void finalizeDebugInfo() { finishDebugInfo(); }

  // 0 = -O0 (default), 1..3 map to LLVM OptimizationLevel::O1..O3.
  void setOptLevel(unsigned level) { opt_level_ = level > 3 ? 3 : level; }
  unsigned optLevel() const { return opt_level_; }

  // `--shared`: export `pub` functions (DLLExport on Windows) and emit PIC objects.
  void setSharedMode(bool shared) { shared_mode_ = shared; }
  bool sharedMode() const { return shared_mode_; }

  // `-g`: attach DWARF/CodeView line tables so debuggers map IR back to .l lines.
  void setDebugInfo(bool on) { debug_info_ = on; }
  bool debugInfo() const { return debug_info_; }

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
    // Set once this specialization's method bodies have been emitted. An early
    // `ensureTypeId` during struct layout registers the functions while the
    // bodies are still deferred, so the body pass must not be skipped.
    bool bodies_emitted = false;
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
  void resettlePendingStructBodies();
  const InterfaceInfo* findInterface(const std::string& name) const;

  llvm::AllocaInst* createEntryAlloca(llvm::Function* fn, llvm::Type* ty,
                                      const std::string& name);
  // Local storage for a variable: a coroutine frame slot inside `async fun`
  // bodies (survives `await`), a stack alloca everywhere else.
  llvm::Value* createLocalStorage(llvm::Function* fn, llvm::Type* ty, const std::string& name);
  // Pointee type of a local slot (alloca or coroutine frame GEP).
  llvm::Type* localSlotType(llvm::Value* slot);
  // Look up a C ABI symbol previously declared via `extern module` (no auto-create).
  llvm::Function* requireExtern(const std::string& name);

  // [performance-first] Replace `l_hash_string` / `l_str_eq` calls with inline IR. The C
  // runtime implementations stay as-is (other front ends / FFI users rely on
  // them); Codegen simply stops emitting the call in favor of straight-line
  // instructions, which removes the out-of-module indirection from every Map
  // probe. Returns nullptr when the callee is not one of the two.
  llvm::Value* tryEmitInlinedStringBuiltin(const ast::CallExpr& call,
                                           const std::vector<llvm::Value*>& args);

  // FNV-1a (64-bit) over a C string of `len` bytes, emitted as a load/xor/mul
  // loop. `len` is i64.
  llvm::Value* emitInlineStringHash(llvm::Value* cstr, llvm::Value* len,
                                    const std::string& name_hint);

  // Byte-wise C-string equality, emitted as a compare loop. NULL-safe like the
  // runtime version: equal pointers are equal, any NULL operand is not equal.
  llvm::Value* emitInlineStringEq(llvm::Value* a, llvm::Value* b,
                                  const std::string& name_hint);
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
  // Type of a destructuring `match` arm binding (array element or field type).
  std::string matchDestructureArmType(const ast::MatchBranch& br,
                                      const ast::MatchExpr& m) const;
  // Return type of `ClassName.method(...)`, static methods before assoc funcs.
  std::string staticMethodReturnType(const std::string& class_name,
                                     const std::string& method) const;
  // Declared type name of the first parameter of `name` in `cls`.
  std::string firstParamTypeName(const ast::ClassDeclStmt& cls,
                                 const std::string& name) const;
  // Address of a field of the current class via `this` (nullptr if absent).
  llvm::Value* fieldAddressOnThis(const std::string& field,
                                  const ast::SourceLocation& loc);
  // Address of one field slot on `receiver` (follows a pointer-typed field once).
  llvm::Value* fieldSlotAddress(const ClassInfo& owner, llvm::Value* receiver,
                                size_t field_idx, const std::string& name,
                                llvm::Type* field_ty);
  // Loads a field of the current class through `this`.
  llvm::Value* loadFieldOnThis(const std::string& field_name);
  // AST declaration of `name` inside `info`, if present.
  const ast::FuncDeclStmt* findMethodAst(const ClassInfo& info,
                                         const std::string& name) const;
  // Converts each call argument to its parameter type, appending to `out`.
  bool emitMethodArgs(const ast::MethodCallExpr& call,
                      const ast::FuncDeclStmt* method_ast, const ClassInfo& callee_info,
                      llvm::Function* target, unsigned first_param_consumed,
                      std::vector<llvm::Value*>& out);
  // Emits the call; returns the value (0 for void) or nullptr on error.
  llvm::Value* finishMethodCall(llvm::Function* target, std::vector<llvm::Value*>& args,
                                const std::string& what);
  // `this.field.method(...)` where the receiver is an enclosing-class field.
  llvm::Value* emitMethodCallOnField(const ast::MethodCallExpr& call,
                                     const std::string& field_name);
  // Static return type of `recv_ty.method` (instance + single delegate promotion).
  std::string methodReturnTypeOn(const std::string& recv_ty,
                                 const std::string& method) const;
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
  // Call `init(...)` for `Class(...)` / `pkg::Class(...)` constructor sugar.
  llvm::Value* emitClassInitCall(const ClassInfo& info, const ast::NewExpr& neu,
                                 llvm::Value* this_ptr, const std::vector<ast::CallArg>& args);
  // Copy a class value from its stack slot into a destination stack slot.
  bool initStackClassFromPtr(llvm::Value* alloca, const ClassInfo& info,
                             llvm::Value* src_ptr, const std::string& name);
  // Heap-allocate + init a class instance (auto-free / `drop()` owners).
  llvm::Value* emitHeapClassAllocAndInit(const ClassInfo& info, const ast::NewExpr& neu,
                                         const std::string& name);
  // Build a stack class value in `slot` from the decl's initializer.
  bool emitStackClassInit(llvm::AllocaInst* slot, const ClassInfo& info,
                          const ast::VarDeclStmt& decl);
  // Emit `init(args)` with the resolved this* as first parameter.
  bool emitInitArgs(const ClassInfo& info, const ast::NewExpr& neu, llvm::Function* init_fn,
                    llvm::Value* base);
  // Shared lowering: explicit arg list + optional init signature AST.
  llvm::Value* emitInitArgsWith(const ClassInfo& info, const ast::NewExpr& neu,
                                llvm::Function* init_fn, const ast::FuncDeclStmt* init_ast,
                                llvm::Value* base, const std::vector<ast::CallArg>& args);
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
  // [dsl] `app { ... }` / `app(type: App) { ... }` — build root inline.
  llvm::Value* emitConfigBlockCall(const ast::CallExpr& call,
                                   const ast::LambdaExpr& block);
  // [dsl] Inline a bare-brace config block at its call site with `this`
  // bound to `root` (a live class object pointer). Returns false on error.
  bool emitConfigBlockInline(const ast::LambdaExpr& body, llvm::Value* root,
                             const std::string& class_name);
  // [dsl] Recursively `init` class-typed fields so every nested section
  // has a live receiver before the config block body runs.
  bool initConfigSections(const ClassInfo& info, llvm::Value* base, int depth);
  // [dsl] Resolve `name { ... }` inside a block: getter → existing child,
  // otherwise a null-initialised `name_` field constructed in place.
  llvm::Value* emitConfigSectionPtr(const std::string& field_name,
                                    const std::string& class_name,
                                    const ast::SourceLocation& loc);
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
  // Emit method bodies deferred during the struct pass (declarations exist).
  bool drainPendingGenericBodies();
  // Max nesting for `G<...>` instantiation (Array<Optional<i32>> = 2).
  static constexpr unsigned kMaxGenericDepth = 2;
  // Syntactic nesting count of a type name (`Array<Optional<i32>>` → 2).
  static unsigned genericNestingDepth(const std::string& name);
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
  // Widen/narrow a scalar to the declared storage type (i32 → i64 → f64).
  llvm::Value* coerceScalarTo(llvm::Value* v, llvm::Type* target);
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
  // Raw-pointer aliases of heap locals: `ptr p = obj as ptr` records p → obj so an
  // escape through `p` also cancels the owning heap local's auto-free.
  std::unordered_map<std::string, std::string> ptr_aliases_;
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
  // `mod::fn(...)` — resolves through module_exports_ (pub-only) then issues the
  // same ABI call the unprefixed form would.
  llvm::Value* emitScopedCall(const ast::ScopedCallExpr& call);
  // `a::b::fn(...)` — exported-function call shared by scoped and qualified forms.
  llvm::Value* emitExportedCall(const std::string& scope, const std::vector<ast::CallArg>& args,
                                const std::string& display, const ast::SourceLocation& loc);
  // `a::b::X.m()` / aliased `X.m()` — namespace chain resolves to a module export.
  llvm::Value* emitQualifiedFreeCall(const ast::MethodCallExpr& call,
                                     const ast::IdentifierExpr& id);
  // Namespace export reached through a `.` receiver — same ABI as a free call.
  llvm::Value* emitMethodAsFreeCall(const ast::FuncDeclStmt& fn,
                                    const ast::MethodCallExpr& call);
  // String operand → its raw C string pointer (spills value-typed operands).
  llvm::Value* stringOperandToCStr(llvm::Value* v, const std::string& ty,
                                   const ast::Expr& e);
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
  // `async fun` — hand-written stackless state machine (no LLVM CoroSplit).
  //
  //   f(params) -> ptr              ; entry: heap frame + task, drives state 0
  //   f$step(ptr frame) -> void     ; the state machine (switch on frame.state)
  //
  // MVP: exactly one `await` per async function, never inside a loop. The
  // awaited task is drained by `l_task_await`, which also keeps the frame and
  // state machine exercised end to end.
  bool emitAsyncFuncDecl(const ast::FuncDeclStmt& fn_ast);
  bool emitCoroutineBody(const ast::FuncDeclStmt& fn_ast);
  // True while lowering the body of an `async fun`: frame slots replace stack
  // allocas and `return` publishes the frame's result slot.
  bool in_coroutine_ = false;
  // Frame layout `{ i32 state, ptr task, T result, <params + locals>, ptr await }`.
  llvm::StructType* coro_frame_ty_ = nullptr;
  unsigned coro_result_idx_ = 2;
  // Frame slot holding the awaited task handle across the suspend point.
  unsigned coro_await_task_idx_ = 0;
  // `ptr` to the frame inside `f$step` (its single argument).
  llvm::Value* coro_frame_ = nullptr;
  // Frame slot index of every parameter/local of the current coroutine.
  std::unordered_map<std::string, unsigned> coro_local_slots_;
  // Cached `getelementptr frame, 0, idx` for each slot, created in the
  // dispatch block so it dominates the body and the post-`await` continuation.
  std::unordered_map<std::string, llvm::Value*> coro_slot_addrs_;
  // Completion block: publishes the result and retires the task.
  llvm::BasicBlock* coro_done_bb_ = nullptr;
  // Resume block reached from state 1 of the dispatch switch.
  llvm::BasicBlock* coro_resume_bb_ = nullptr;
  // Dispatch `switch` on `frame.state`; state 1 is added when `await` is seen.
  llvm::SwitchInst* coro_dispatch_sw_ = nullptr;
  // Continuation block of the (single) `await`: where state 1 resumes.
  llvm::BasicBlock* coro_await_cont_bb_ = nullptr;
  // Set once an `await` has been lowered (second one is a compile error).
  bool coro_await_emitted_ = false;
  // `await expr` — drive the awaited task, yield its payload.
  llvm::Value* emitAwaitExpr(const ast::AwaitExpr& expr);
  // Copy the value into the frame's result slot and jump to completion.
  void publishCoroResult(llvm::Value* value);
  void emitCoroReturn();
  // Destructuring patterns (`User { name }` / `[a, b]`): bind fields of `obj`
  // to locals, recursing into nested patterns. `slot` holds the object pointer.
  // Returns false on error; bindings are recorded in `bound` for later cleanup.
  bool emitPatternBindings(const ast::MatchPattern& pat, llvm::Value* slot,
                           std::vector<std::pair<std::string, llvm::Value*>>& bound);
  // Scrutinee type name of the match currently being lowered (array patterns
  // need `Array<T>` to find the element type).
  std::string scrut_type_name_;
  void restorePatternBindings(
      const std::vector<std::pair<std::string, llvm::Value*>>& bound,
      const std::unordered_map<std::string, std::string>& saved_types,
      const std::unordered_set<std::string>& saved_heap);
  // Implicit auto-boxing into Variant (`set("k", 42)` → Variant.from_i32(42)).
  // `expected` is the callee's LLVM parameter type (ptr for by-reference classes).
  llvm::Value* emitImplicitVariantBox(const ast::Expr& expr, llvm::Type* expected);
  // Value-producing match: arms yield expressions merged by a PHI node.
  llvm::Value* emitMatchExpr(const ast::MatchExpr& expr);
  bool emitWhile(const ast::WhileStmt& stmt);
  bool emitBreak(const ast::BreakStmt& stmt);
  bool emitContinue(const ast::ContinueStmt& stmt);
  void flushLoopJumpScopes();
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
  llvm::Value* emitMethodCall(const ast::MethodCallExpr& call,
                              const ast::Expr* object_override = nullptr);
  // [dsl] Invoke `call.method` on an already-resolved class object.
  llvm::Value* emitClassMethodCall(const ast::MethodCallExpr& call, const ClassInfo& cinfo,
                                   llvm::Value* self_ptr, const std::string& class_name);
  // `obj?.m(...)` — short-circuits to an empty Optional when the receiver is one.
  llvm::Value* emitSafeMethodCall(const ast::MethodCallExpr& call);
  // `obj?.field` — short-circuits to an empty Optional<FieldType> when the
  // receiver Optional is empty, mirroring emitSafeMethodCall's skeleton.
  llvm::Value* emitSafeMemberAccess(const ast::MemberAccessExpr& access);

  void error(const std::string& message);
  void error(const ast::SourceLocation& loc, const std::string& message);
  // Set by the first `error(...)`: body emission stops so the half-built
  // module is never lowered (backend crashes mask the real diagnostic).
  bool in_error_ = false;
  void ensureDataLayout();
  ast::SourceLocation defaultLoc() const;

  static std::string mangleName(const std::string& module_name, const std::string& name,
                                bool is_pub);
  std::string resolveFuncIrName(const std::string& callee) const;

  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> builder_;
  std::unordered_map<std::string, llvm::Value*> named_values_;
  std::unordered_map<std::string, std::string> local_types_;  // Compile-time `const` — inlined, no GlobalVariable.
  // Unique suffix for `??` temporaries.
  std::size_t coalesce_tmp_counter_ = 0;
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
  // Heap String locals whose `data_` C string is owned (from concat / *_to_str).
  std::unordered_set<std::string> owned_string_locals_;
  // Locals that auto-cleanup via l_arena_free (not l_free).
  std::unordered_set<std::string> arena_locals_;
  // Per-scope names of `heap`-allocated locals still needing auto l_free.
  std::vector<std::vector<std::string>> auto_free_scopes_;
  // Class params without trailing '!': reject field / index stores through them.
  std::unordered_set<std::string> readonly_refs_;
  std::unordered_map<std::string, ClassInfo> classes_;
  // Generic specializations whose field list mentioned a class that was still
  // mid-registration (opaque). Re-`setBody` them once the class bodies settle,
  // otherwise LLVM keeps them unsized ("GEP into unsized type").
  std::vector<std::pair<std::string, std::vector<llvm::Type*>>> pending_struct_bodies_;
  // `true` while class struct bodies are being laid out: specializations may
  // declare method signatures but must defer bodies until every body is sized.
  bool in_struct_pass_ = false;
  std::vector<std::string> pending_generic_bodies_;
  // Forward-declared functions that return a type only materialized by the class
  // passes below (`http::make_config() -> Config`). Declared as ptr already.
  std::vector<const ast::FuncDeclStmt*> deferred_return_types_;
  // Classes whose field layout hit a still-opaque struct; retried after each
  // successful layout so declaration order does not matter.
  std::vector<const ast::ClassDeclStmt*> deferred_bodies_;
  std::unordered_map<std::string, InterfaceInfo> interfaces_;
  std::unordered_map<std::string, const ast::FuncDeclStmt*> generic_templates_;
  std::unordered_map<std::string, const ast::FuncDeclStmt*> concrete_funcs_;  // for KW reorder
  std::unordered_map<std::string, const ast::ClassDeclStmt*> generic_class_templates_;
  std::unordered_map<std::string, llvm::Function*> specialized_funcs_;
  std::unordered_map<std::string, std::string> type_subst_;
  // Cross-module symbol tables: short name → LLVM IR name.
  std::unordered_map<std::string, std::string> public_funcs_;
  std::unordered_map<std::string, std::string> private_funcs_;  // "mod::name" → IR
  // Cross-module exports: module stem → (pub name → declaration).
  std::unordered_map<std::string, std::unordered_map<std::string, const ast::FuncDeclStmt*>>
      module_exports_;
  // `use` aliases: alias → namespace scope (`Server` → `net::http::Server`).
  std::unordered_map<std::string, std::string> use_aliases_;
  std::string current_module_;
  llvm::StructType* fat_ptr_ty_ = nullptr;

  // Defer scopes: outer -> inner. Each scope holds DeferStmt* in registration order.
  std::vector<std::vector<const ast::DeferStmt*>> defer_scopes_;
  // Innermost-first loop targets: `continue` jumps to cond, `break` to end.
  // `base_scope_depth` is defer_scopes_.size() at loop-body entry, so a jump can
  // flush the blocks it skips without touching outer scopes.
  struct LoopTargets {
    llvm::BasicBlock* cond_bb = nullptr;
    llvm::BasicBlock* end_bb = nullptr;
    size_t base_scope_depth = 0;
  };
  std::vector<LoopTargets> loop_stack_;
  // Currently held `sync` mutexes (global lock ptrs); unlocked on scope exit / return.
  std::vector<llvm::Value*> held_sync_locks_;

  std::string current_class_;
  llvm::Value* current_this_ = nullptr;
  // [dsl] Bare-brace block handed to a config-block function; bound by
  // the owning call site, which also supplies the config root.
  const ast::LambdaExpr* pending_config_block_ = nullptr;
  std::string pending_config_expected_;
  bool current_method_is_static_ = false;
  // Enclosing function return type name (e.g. Result<i32,String>) for `expr?`.
  std::string current_fn_return_type_;
  // Contextual unboxing: an expression of type `Variant` used where the
  // context expects String/i32/i64/f64/bool → `v.as_<t>()`, panicking when the
  // tag mismatches. `expected` is either an L type name or an LLVM type (for
  // inferred `:=` / builtin call sites pass the name; for typed formals pass
  // both). Returns nullptr when no unboxing applies (caller falls back).
  llvm::Value* emitContextualVariantUnbox(const ast::Expr& expr,
                                          const std::string& expected_type,
                                          llvm::Type* rty,
                                          bool immediate = false);
  // `print(v)` where v:Variant — branch on `type_tag_` and call the matching
  // l_print_* helper, so a dynamically tagged value prints its payload.
  llvm::Value* emitPrintVariant(const ast::Expr& arg_expr, llvm::Value* var_val,
                                const ast::SourceLocation& loc);
  // `print(opt)` where opt:Optional<Variant> — same dispatch, using the
  // Variant payload when present and a marker when the Optional is empty.
  llvm::Value* emitPrintOptionalVariant(const ast::Expr& arg_expr, llvm::Value* opt_val,
                                        const ast::SourceLocation& loc);
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

  // --- Debug info (`-g`) ---------------------------------------------------
  // One compile unit covers the whole program (L merges every module into a
  // single LLVM module), plus one DISubprogram per emitted function. Line
  // tables come from a DILocation set before each statement.
  bool debug_info_ = false;
  std::unique_ptr<llvm::DIBuilder> di_builder_;
  llvm::DICompileUnit* di_cu_ = nullptr;
  llvm::DIFile* di_file_ = nullptr;
  std::unordered_map<std::string, llvm::DIFile*> di_files_;
  llvm::DISubprogram* di_sp_ = nullptr;   // subprogram of the function being emitted
  llvm::DILexicalBlock* di_scope_ = nullptr;  // innermost debug scope (nullptr → di_sp_)
  unsigned di_stmt_ = 0;                  // smallest DILocation per DISubprogram
  void initDebugInfo(const std::string& module_name);
  void finishDebugInfo();
  llvm::DIFile* debugFile(const std::string& filename);
  llvm::DISubprogram* debugSubprogram(llvm::Function* fn, const ast::FuncDeclStmt& decl,
                                      llvm::DIFile* file);
  void setDebugLoc(const ast::SourceLocation& loc);
  void clearDebugLoc();
};

}  // namespace l::codegen
