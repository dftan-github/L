#include "jit/orc_jit.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/ExecutionEngine/Orc/AbsoluteSymbols.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

// L C runtime — must be linked into the host (`l` executable) for AbsoluteSymbols.
extern "C" {
void* l_alloc(size_t size);
void* l_realloc(void* ptr, size_t new_size);
void l_free(void* ptr);
int32_t l_get_alloc_count(void);
void* l_arena_new(size_t size);
void* l_arena_alloc(void* arena, size_t size);
void l_arena_free(void* arena);
void l_store_i32(void* p, int32_t v);
int32_t l_load_i32(void* p);
void l_store_f64(void* p, double v);
double l_load_f64(void* p);
void l_store_ptr(void* p, void* v);
void* l_load_ptr(void* p);

void l_print_int(int32_t val);
void l_print_i64(int64_t val);
void l_print_f64(double val);
void l_print_bool(int32_t val);
void l_print_str(const char* s);
char* l_read_line(void);
void* l_str_concat(const char* a, const char* b);
void* l_int_to_str(int32_t val);
void* l_i64_to_str(int64_t val);
void* l_f64_to_str(double val);
void* l_bool_to_str(int32_t val);
uint64_t l_hash_string(const char* s);
int32_t l_hash_to_bucket(uint64_t hash, int32_t capacity);
int32_t l_str_eq(const char* a, const char* b);
int32_t l_str_len(const char* s);
int32_t l_str_char_at(const char* s, int32_t i);
char* l_str_substr(const char* s, int32_t start, int32_t len);
int32_t l_str_to_i32(const char* s);
int32_t l_ptr_is_null(void* p);
void* l_file_open(const char* path, const char* mode);
int32_t l_file_read(void* file, void* buffer, int32_t size);
int32_t l_file_write(void* file, const void* buffer, int32_t size);
void l_file_close(void* file);
int32_t l_file_size(void* file);
void l_store_u8(void* p, int32_t index, int32_t v);

void l_init_args(int32_t argc, char** argv);
int32_t l_get_arg_count(void);
char* l_get_arg(int32_t index);
char* l_get_env(const char* name);
int32_t l_file_exists(const char* path);
int32_t l_create_dir(const char* path);
int32_t l_remove_file(const char* path);
int32_t l_remove_dir(const char* path);
int64_t l_time_now_sec(void);
int64_t l_time_now_ms(void);
char* l_format_time(int64_t timestamp, const char* format_str);
int32_t l_run_command(const char* cmd);
int32_t l_regex_match(const char* pattern, const char* text);

void* l_mutex_init(void);
void l_mutex_lock(void* mutex);
void l_mutex_unlock(void* mutex);
void l_mutex_free(void* mutex);
void* l_mutex_global(void);
void* l_spawn(void* fn_ptr, void* arg_ptr);
void l_thread_join(void* handle);
void l_sleep_ms(int32_t ms);
int64_t l_get_lock_count(void);
int64_t l_get_unlock_count(void);
void l_reset_lock_counts(void);
int32_t l_atomic_add_i32(int32_t* ptr, int32_t val);
int32_t l_atomic_load_i32(int32_t* ptr);
void l_atomic_store_i32(int32_t* ptr, int32_t val);

typedef void (*l_callback_i32_fn)(int32_t);
void l_register_callback(l_callback_i32_fn cb);
void l_trigger_callback(int32_t val);

int32_t l_socket_create(void);
int32_t l_socket_bind_and_listen(int32_t fd, int32_t port);
int32_t l_socket_accept(int32_t fd);
int32_t l_socket_connect(int32_t fd, const char* ip, int32_t port);
int32_t l_socket_send(int32_t fd, const void* data, int32_t len);
int32_t l_socket_recv(int32_t fd, void* buffer, int32_t len);
void l_socket_close(int32_t fd);

// Configuration-as-code host hooks (implemented in package/config_host.cpp).
void add_dependency(const char* name, const char* version);
const char* get_build_mode(void);
}

namespace l::jit {
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

void logError(llvm::Error err) {
  llvm::handleAllErrors(std::move(err), [](const llvm::ErrorInfoBase& e) {
    std::cerr << "[ERROR] <jit>:1:1 - " << e.message() << '\n';
  });
}

llvm::Error defineRuntimeSymbols(llvm::orc::LLJIT& jit) {
  llvm::orc::SymbolMap map;
  auto intern = [&](const char* name, void* addr) {
    map[jit.mangleAndIntern(name)] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(addr), llvm::JITSymbolFlags::Exported);
  };

  intern("l_alloc", reinterpret_cast<void*>(&l_alloc));
  intern("l_realloc", reinterpret_cast<void*>(&l_realloc));
  intern("l_free", reinterpret_cast<void*>(&l_free));
  intern("l_get_alloc_count", reinterpret_cast<void*>(&l_get_alloc_count));
  intern("l_arena_new", reinterpret_cast<void*>(&l_arena_new));
  intern("l_arena_alloc", reinterpret_cast<void*>(&l_arena_alloc));
  intern("l_arena_free", reinterpret_cast<void*>(&l_arena_free));
  intern("l_store_i32", reinterpret_cast<void*>(&l_store_i32));
  intern("l_load_i32", reinterpret_cast<void*>(&l_load_i32));
  intern("l_store_f64", reinterpret_cast<void*>(&l_store_f64));
  intern("l_load_f64", reinterpret_cast<void*>(&l_load_f64));
  intern("l_store_ptr", reinterpret_cast<void*>(&l_store_ptr));
  intern("l_load_ptr", reinterpret_cast<void*>(&l_load_ptr));

  intern("l_print_int", reinterpret_cast<void*>(&l_print_int));
  intern("l_print_i64", reinterpret_cast<void*>(&l_print_i64));
  intern("l_print_f64", reinterpret_cast<void*>(&l_print_f64));
  intern("l_print_bool", reinterpret_cast<void*>(&l_print_bool));
  intern("l_print_str", reinterpret_cast<void*>(&l_print_str));
  intern("l_read_line", reinterpret_cast<void*>(&l_read_line));
  intern("l_str_concat", reinterpret_cast<void*>(&l_str_concat));
  intern("l_int_to_str", reinterpret_cast<void*>(&l_int_to_str));
  intern("l_i64_to_str", reinterpret_cast<void*>(&l_i64_to_str));
  intern("l_f64_to_str", reinterpret_cast<void*>(&l_f64_to_str));
  intern("l_bool_to_str", reinterpret_cast<void*>(&l_bool_to_str));
  intern("l_hash_string", reinterpret_cast<void*>(&l_hash_string));
  intern("l_hash_to_bucket", reinterpret_cast<void*>(&l_hash_to_bucket));
  intern("l_str_eq", reinterpret_cast<void*>(&l_str_eq));
  intern("l_str_len", reinterpret_cast<void*>(&l_str_len));
  intern("l_str_char_at", reinterpret_cast<void*>(&l_str_char_at));
  intern("l_str_substr", reinterpret_cast<void*>(&l_str_substr));
  intern("l_str_to_i32", reinterpret_cast<void*>(&l_str_to_i32));
  intern("l_ptr_is_null", reinterpret_cast<void*>(&l_ptr_is_null));
  intern("l_file_open", reinterpret_cast<void*>(&l_file_open));
  intern("l_file_read", reinterpret_cast<void*>(&l_file_read));
  intern("l_file_write", reinterpret_cast<void*>(&l_file_write));
  intern("l_file_close", reinterpret_cast<void*>(&l_file_close));
  intern("l_file_size", reinterpret_cast<void*>(&l_file_size));
  intern("l_store_u8", reinterpret_cast<void*>(&l_store_u8));

  intern("l_init_args", reinterpret_cast<void*>(&l_init_args));
  intern("l_get_arg_count", reinterpret_cast<void*>(&l_get_arg_count));
  intern("l_get_arg", reinterpret_cast<void*>(&l_get_arg));
  intern("l_get_env", reinterpret_cast<void*>(&l_get_env));
  intern("l_file_exists", reinterpret_cast<void*>(&l_file_exists));
  intern("l_create_dir", reinterpret_cast<void*>(&l_create_dir));
  intern("l_remove_file", reinterpret_cast<void*>(&l_remove_file));
  intern("l_remove_dir", reinterpret_cast<void*>(&l_remove_dir));
  intern("l_time_now_sec", reinterpret_cast<void*>(&l_time_now_sec));
  intern("l_time_now_ms", reinterpret_cast<void*>(&l_time_now_ms));
  intern("l_format_time", reinterpret_cast<void*>(&l_format_time));
  intern("l_run_command", reinterpret_cast<void*>(&l_run_command));
  intern("l_regex_match", reinterpret_cast<void*>(&l_regex_match));

  intern("l_mutex_init", reinterpret_cast<void*>(&l_mutex_init));
  intern("l_mutex_lock", reinterpret_cast<void*>(&l_mutex_lock));
  intern("l_mutex_unlock", reinterpret_cast<void*>(&l_mutex_unlock));
  intern("l_mutex_free", reinterpret_cast<void*>(&l_mutex_free));
  intern("l_mutex_global", reinterpret_cast<void*>(&l_mutex_global));
  intern("l_spawn", reinterpret_cast<void*>(&l_spawn));
  intern("l_thread_join", reinterpret_cast<void*>(&l_thread_join));
  intern("l_sleep_ms", reinterpret_cast<void*>(&l_sleep_ms));
  intern("l_get_lock_count", reinterpret_cast<void*>(&l_get_lock_count));
  intern("l_get_unlock_count", reinterpret_cast<void*>(&l_get_unlock_count));
  intern("l_reset_lock_counts", reinterpret_cast<void*>(&l_reset_lock_counts));
  intern("l_atomic_add_i32", reinterpret_cast<void*>(&l_atomic_add_i32));
  intern("l_atomic_load_i32", reinterpret_cast<void*>(&l_atomic_load_i32));
  intern("l_atomic_store_i32", reinterpret_cast<void*>(&l_atomic_store_i32));

  intern("l_register_callback", reinterpret_cast<void*>(&l_register_callback));
  intern("l_trigger_callback", reinterpret_cast<void*>(&l_trigger_callback));

  intern("l_socket_create", reinterpret_cast<void*>(&l_socket_create));
  intern("l_socket_bind_and_listen", reinterpret_cast<void*>(&l_socket_bind_and_listen));
  intern("l_socket_accept", reinterpret_cast<void*>(&l_socket_accept));
  intern("l_socket_connect", reinterpret_cast<void*>(&l_socket_connect));
  intern("l_socket_send", reinterpret_cast<void*>(&l_socket_send));
  intern("l_socket_recv", reinterpret_cast<void*>(&l_socket_recv));
  intern("l_socket_close", reinterpret_cast<void*>(&l_socket_close));

  // lconf.l Configuration-as-Code builtins (AbsoluteSymbols → host C++).
  intern("add_dependency", reinterpret_cast<void*>(&add_dependency));
  intern("get_build_mode", reinterpret_cast<void*>(&get_build_mode));

  return jit.getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(map)));
}

// Clone IR into a fresh LLVMContext via bitcode (isolates ThreadSafeContext per run).
llvm::Expected<llvm::orc::ThreadSafeModule> cloneToThreadSafeModule(llvm::Module& src) {
  llvm::SmallVector<char, 0> buf;
  llvm::raw_svector_ostream os(buf);
  llvm::WriteBitcodeToFile(src, os);

  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto mem = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(buf.data(), buf.size()), src.getName());
  auto parsed = llvm::parseBitcodeFile(mem->getMemBufferRef(), *ctx);
  if (!parsed) {
    return parsed.takeError();
  }
  (*parsed)->setDataLayout(src.getDataLayout());
  (*parsed)->setTargetTriple(src.getTargetTriple());
  return llvm::orc::ThreadSafeModule(std::move(*parsed), std::move(ctx));
}

}  // namespace

int runModuleMain(llvm::Module& module) {
  ensureNativeTargets();

  auto jit_expected = llvm::orc::LLJITBuilder().create();
  if (!jit_expected) {
    logError(jit_expected.takeError());
    return -1;
  }
  std::unique_ptr<llvm::orc::LLJIT> jit = std::move(*jit_expected);

  if (auto err = defineRuntimeSymbols(*jit)) {
    logError(std::move(err));
    return -1;
  }

  // Host CRT (memcpy/memset/…) via in-process search — works for DLLs on Windows.
  {
    auto gen = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit->getDataLayout().getGlobalPrefix());
    if (!gen) {
      logError(gen.takeError());
      return -1;
    }
    jit->getMainJITDylib().addGenerator(std::move(*gen));
  }

  auto tsm = cloneToThreadSafeModule(module);
  if (!tsm) {
    logError(tsm.takeError());
    return -1;
  }

  if (auto err = jit->addIRModule(std::move(*tsm))) {
    logError(std::move(err));
    return -1;
  }

  auto sym = jit->lookup("main");
  if (!sym) {
    logError(sym.takeError());
    return -1;
  }

  // L main is always CRT-shaped: int main(int argc, char** argv).
  using MainFn = int (*)(int, char**);
  MainFn main_fn = sym->toPtr<MainFn>();
  if (!main_fn) {
    std::cerr << "[ERROR] <jit>:1:1 - main symbol is null\n";
    return -1;
  }
  return main_fn(0, nullptr);
}

}  // namespace l::jit
