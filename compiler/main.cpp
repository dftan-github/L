#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ast/ast.hpp"
#include "ast/token.hpp"
#include "codegen/codegen.hpp"
#include "compile/compile.hpp"
#include "diagnostic/diagnostic.hpp"
#include "doc/gen_doc.hpp"
#include "iface/gen_interface.hpp"
#include "jit/orc_jit.hpp"
#include "lexer/lexer.hpp"
#include "module/loader.hpp"
#include "package/lconf_runner.hpp"
#include "package/resolve.hpp"
#include "semantic/semantic.hpp"

#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#ifndef L_BUILD_DIR
#define L_BUILD_DIR "."
#endif

#ifndef L_SOURCE_DIR
#define L_SOURCE_DIR "."
#endif

#ifndef L_CLANG_PATH
#define L_CLANG_PATH "clang++"
#endif

namespace fs = std::filesystem;

namespace {

enum class CliCommand { Build, Run, Repl, Eval, GenInterface, Doc };

void printUsage(const char* argv0) {
  std::cerr << "Usage:\n"
            << "  " << argv0 << " build [options] <file.l>...   # compile to executable / library\n"
            << "  " << argv0 << " run   [options] <file.l>...   # compile and run immediately\n"
            << "  " << argv0 << " repl                          # interactive ORC JIT REPL\n"
            << "  " << argv0 << " eval  '<code>'                # ORC JIT execute a string\n"
            << "  " << argv0 << " gen-interface <file.l> -o <out.li>  # emit public .li signatures\n"
            << "  " << argv0 << " doc <file.l> -o <out.md>      # emit Markdown API docs for pub surface\n"
            << "\n"
            << "Compatibility (no subcommand):\n"
            << "  " << argv0 << " <file.l>...                   # same as `build`\n"
            << "  " << argv0 << " --repl / --eval '<code>'      # same as `repl` / `eval`\n"
            << "\n"
            << "Common options (build / run):\n"
            << "  -o <path>   --shared   --emit-llvm   --emit-obj\n"
            << "  --dump-tokens   --dump-ast   -O0|-O1|-O2|-O3|--release\n"
            << "  -                          # read source from stdin\n"
            << "\n"
            << "Multiple .l inputs merge into one LLVM module linked with l_runtime.\n"
            << "`run` / `eval` auto-prepend stdlib memory.l + io.l + thread.l when missing.\n"
            << "`build` without -o writes <primary>.exe next to the entry file.\n";
}

bool endsWithDotL(std::string_view s) {
  return s.size() >= 2 && s[s.size() - 2] == '.' && (s.back() == 'l' || s.back() == 'L');
}

bool looksLikeSubcommand(std::string_view arg) {
  return arg == "build" || arg == "run" || arg == "repl" || arg == "eval" ||
         arg == "gen-interface" || arg == "doc";
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string readAllStdin() {
  std::ostringstream ss;
  ss << std::cin.rdbuf();
  return ss.str();
}

void dumpTokens(const std::vector<l::ast::Token>& tokens) {
  for (const l::ast::Token& tok : tokens) {
    std::cout << l::ast::toString(tok.type) << " \"" << tok.lexeme << "\" @ "
              << tok.loc.filename << ':' << tok.loc.line << ':' << tok.loc.column
              << '\n';
  }
}

std::string runtimeLibPath() {
  fs::path p = fs::path(L_BUILD_DIR) / "runtime";
#ifdef _WIN32
  p /= "l_runtime.lib";
#else
  p /= "libl_runtime.a";
#endif
  return p.string();
}

std::string clangPath() {
  return L_CLANG_PATH;
}

bool linkWithRuntime(const std::string& object_path, const std::string& out_path,
                     unsigned opt_level, bool shared,
                     const std::vector<std::string>& extra_libs = {}) {
  const std::string clang = clangPath();
  const std::string runtime = runtimeLibPath();
  if (!fs::exists(runtime)) {
    std::cerr << "runtime library not found: " << runtime
              << "\nBuild l_runtime first (cmake --build --target l_runtime).\n";
    return false;
  }

  {
    std::error_code ec;
    fs::create_directories(fs::path(out_path).parent_path(), ec);
  }

  const std::string opt_flag =
      opt_level == 0 ? "-O0" : (opt_level == 1 ? "-O1" : (opt_level == 2 ? "-O2" : "-O3"));

#ifdef _WIN32
  const char* vcvars =
      "C:\\Program Files\\Microsoft Visual Studio\\18\\Insiders\\VC\\Auxiliary\\Build\\vcvars64.bat";
  if (!fs::exists(vcvars)) {
    std::cerr << "vcvars64.bat not found: " << vcvars
              << "\nOpen a VS x64 Native Tools shell, or update the path in main.cpp.\n";
    return false;
  }
  const fs::path bat_path = fs::temp_directory_path() / "l_link_tmp.bat";
  {
    std::ofstream bat(bat_path);
    if (!bat) {
      std::cerr << "could not write temp link script: " << bat_path << '\n';
      return false;
    }
    bat << "@echo off\r\n";
    bat << "call \"" << vcvars << "\" >nul\r\n";
    bat << "if errorlevel 1 exit /b 1\r\n";
    // `-shared` + static l_runtime → self-contained DLL exporting pub symbols.
    bat << '"' << clang << "\" " << opt_flag;
    if (shared) {
      bat << " -shared";
    }
    bat << " \"" << object_path << "\" \"" << runtime << "\"";
    for (const std::string& lib : extra_libs) {
      bat << " \"" << lib << "\"";
    }
    // Winsock for l_socket_* (runtime/net.c); clang maps -lws2_32 → ws2_32.lib.
    bat << " -lws2_32";
    bat << " -o \"" << out_path << "\"\r\n";
    bat << "exit /b %ERRORLEVEL%\r\n";
  }
  const std::string cmd_exe = "C:\\Windows\\System32\\cmd.exe";
  const std::string bat_str = bat_path.string();
  std::vector<llvm::StringRef> args = {cmd_exe, "/c", bat_str};
  std::string err;
  const int rc = llvm::sys::ExecuteAndWait(cmd_exe, args, std::nullopt, {}, 0, 0, &err);
  std::error_code rm_ec;
  fs::remove(bat_path, rm_ec);
#else
  // Link pthread for l_spawn / mutex (l_runtime depends on Threads::Threads).
  std::vector<std::string> storage = {clang, opt_flag};
  if (shared) {
    storage.emplace_back("-shared");
    storage.emplace_back("-fPIC");
  }
  storage.push_back(object_path);
  storage.push_back(runtime);
  for (const std::string& lib : extra_libs) {
    storage.push_back(lib);
  }
  storage.emplace_back("-pthread");
  storage.emplace_back("-o");
  storage.push_back(out_path);
  std::vector<llvm::StringRef> args;
  args.reserve(storage.size());
  for (const std::string& s : storage) {
    args.emplace_back(s);
  }
  std::string err;
  const int rc = llvm::sys::ExecuteAndWait(clang, args, std::nullopt, {}, 0, 0, &err);
#endif
  if (rc != 0) {
    std::cerr << "link failed (exit " << rc << "): " << err << '\n';
    std::cerr << "Tried link of " << object_path << " + " << runtime << " -> " << out_path
              << '\n';
    return false;
  }
  return true;
}

bool linkExecutable(const std::string& object_path, const std::string& exe_path,
                    unsigned opt_level, const std::vector<std::string>& extra_libs = {}) {
  return linkWithRuntime(object_path, exe_path, opt_level, /*shared=*/false, extra_libs);
}

bool linkSharedLibrary(const std::string& object_path, const std::string& lib_path,
                       unsigned opt_level, const std::vector<std::string>& extra_libs = {}) {
  return linkWithRuntime(object_path, lib_path, opt_level, /*shared=*/true, extra_libs);
}

std::string trimCopy(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.pop_back();
  }
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  return s.substr(i);
}

bool containsFunMain(const std::string& src) {
  return src.find("fun main") != std::string::npos;
}

// Snippets without an entry point become a one-shot main (expression / statements).
std::string wrapReplSnippet(const std::string& src) {
  if (containsFunMain(src)) {
    return src;
  }
  return "fun main() -> i32 {\n" + src + "\nreturn 0\n}\n";
}

fs::path stdlibPath(const char* name) {
  return fs::path(L_SOURCE_DIR) / "stdlib" / name;
}

// Prepend core stdlib modules when missing (used by `run` / `eval` for one-file DX).
void prependCoreStdlib(std::vector<l::module::SourceUnit>& units) {
  auto hasFilename = [&](const char* filename) -> bool {
    for (const l::module::SourceUnit& u : units) {
      if (u.from_memory) {
        continue;
      }
      if (fs::path(u.name).filename() == filename) {
        return true;
      }
    }
    return false;
  };

  std::vector<l::module::SourceUnit> prefix;
  for (const char* name : {"memory.l", "io.l", "thread.l"}) {
    if (hasFilename(name)) {
      continue;
    }
    const fs::path p = stdlibPath(name);
    if (!fs::exists(p)) {
      continue;
    }
    l::module::SourceUnit u;
    u.from_memory = false;
    u.name = p.string();
    prefix.push_back(std::move(u));
  }
  if (!prefix.empty()) {
    units.insert(units.begin(), std::make_move_iterator(prefix.begin()),
                 std::make_move_iterator(prefix.end()));
  }
}

int runExecutable(const std::string& exe_path) {
  std::vector<llvm::StringRef> args = {exe_path};
  std::string err;
  const int rc = llvm::sys::ExecuteAndWait(exe_path, args, std::nullopt, {}, 0, 0, &err);
  if (rc < 0) {
    std::cerr << "failed to run " << exe_path << ": " << err << '\n';
    return 1;
  }
  return rc;
}

// Compile one REPL/eval snippet to LLVM IR and execute via ORC LLJIT (no .exe).
bool jitExecuteSource(const std::string& full_src, const std::string& virtual_name,
                      unsigned opt_level) {
  l::diag::DiagnosticEngine diags;
  auto ast = l::compile::compileString(full_src, virtual_name, diags);
  if (!ast || diags.hasErrors()) {
    diags.report(std::cerr);
    return false;
  }

  l::semantic::Analyzer analyzer;
  analyzer.analyze(*ast, diags);
  if (diags.hasErrors()) {
    diags.report(std::cerr);
    return false;
  }

  l::codegen::Codegen codegen(virtual_name, &diags);
  codegen.setOptLevel(opt_level);
  if (!codegen.emit(*ast) || diags.hasErrors()) {
    diags.report(std::cerr);
    return false;
  }

  const int rc = l::jit::runModuleMain(codegen.module());
  return rc >= 0;
}

std::string loadReplRuntimePrefix() {
  const std::string mem_src = readFile(stdlibPath("memory.l").string());
  const std::string io_src = readFile(stdlibPath("io.l").string());
  const std::string thr_src = readFile(stdlibPath("thread.l").string());
  if (mem_src.empty() || io_src.empty() || thr_src.empty()) {
    return {};
  }
  return mem_src + "\n" + io_src + "\n" + thr_src + "\n";
}

// ORC JIT one REPL line. On failure, print diagnostics and keep the loop alive.
void evalReplSnippet(const std::string& user_src) {
  const std::string wrapped = wrapReplSnippet(user_src);
  const std::string prefix = loadReplRuntimePrefix();
  if (prefix.empty()) {
    std::cerr << "[ERROR] <repl>:1:1 - cannot load stdlib for REPL "
                 "(check L_SOURCE_DIR/stdlib)\n";
    return;
  }
  (void)jitExecuteSource(prefix + wrapped, "<repl>", /*opt_level=*/0);
}

int runRepl() {
  std::cout << "L REPL — type L statements (auto-wrapped in main). Commands: exit, quit\n";
  for (;;) {
    std::cout << "L> " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
      std::cout << '\n';
      break;
    }
    const std::string trimmed = trimCopy(line);
    if (trimmed.empty()) {
      continue;
    }
    if (trimmed == "exit" || trimmed == "quit") {
      break;
    }
    evalReplSnippet(trimmed);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  bool dump_tokens = false;
  bool dump_ast = false;
  bool emit_llvm = false;
  bool emit_obj = false;
  bool shared_lib = false;
  unsigned opt_level = 0;
  std::string output_path;
  std::vector<l::module::SourceUnit> units;
  bool saw_stdin = false;
  bool saw_eval = false;
  CliCommand cmd = CliCommand::Build;

  int arg_i = 1;
  if (argc >= 2) {
    const std::string_view first = argv[1];
    if (looksLikeSubcommand(first)) {
      if (first == "build") {
        cmd = CliCommand::Build;
      } else if (first == "run") {
        cmd = CliCommand::Run;
      } else if (first == "repl") {
        cmd = CliCommand::Repl;
      } else if (first == "eval") {
        cmd = CliCommand::Eval;
      } else if (first == "gen-interface") {
        cmd = CliCommand::GenInterface;
      } else if (first == "doc") {
        cmd = CliCommand::Doc;
      }
      arg_i = 2;
      // `l eval '<code>'` — code is the next positional (options may precede it).
    }
  }

  for (; arg_i < argc; ++arg_i) {
    const std::string_view arg = argv[arg_i];
    if (arg == "--repl") {
      cmd = CliCommand::Repl;
      continue;
    }
    if (arg == "--dump-tokens") {
      dump_tokens = true;
      continue;
    }
    if (arg == "--dump-ast") {
      dump_ast = true;
      continue;
    }
    if (arg == "--emit-llvm") {
      emit_llvm = true;
      continue;
    }
    if (arg == "--emit-obj") {
      emit_obj = true;
      continue;
    }
    if (arg == "--shared" || arg == "-shared") {
      shared_lib = true;
      continue;
    }
    if (arg == "-O0") {
      opt_level = 0;
      continue;
    }
    if (arg == "-O1") {
      opt_level = 1;
      continue;
    }
    if (arg == "-O2") {
      opt_level = 2;
      continue;
    }
    if (arg == "-O3" || arg == "--release") {
      opt_level = 3;
      continue;
    }
    if (arg == "-o") {
      if (arg_i + 1 >= argc) {
        std::cerr << "-o requires an output path\n";
        return 1;
      }
      output_path = argv[++arg_i];
      continue;
    }
    if (arg == "--eval") {
      if (arg_i + 1 >= argc) {
        std::cerr << "--eval requires a source string\n";
        return 1;
      }
      if (saw_eval) {
        std::cerr << "only one eval source is allowed\n";
        return 1;
      }
      saw_eval = true;
      cmd = CliCommand::Eval;
      l::module::SourceUnit unit;
      unit.from_memory = true;
      unit.name = "<eval>";
      unit.source = argv[++arg_i];
      units.push_back(std::move(unit));
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    }
    if (arg == "-") {
      if (saw_stdin) {
        std::cerr << "only one '-' (stdin) source is allowed\n";
        return 1;
      }
      saw_stdin = true;
      l::module::SourceUnit unit;
      unit.from_memory = true;
      unit.name = "<stdin>";
      unit.source = readAllStdin();
      units.push_back(std::move(unit));
      continue;
    }

    // `l eval '<code>'` — first non-option positional is the source string.
    if (cmd == CliCommand::Eval && !saw_eval && !endsWithDotL(arg) && arg != "-") {
      saw_eval = true;
      l::module::SourceUnit unit;
      unit.from_memory = true;
      unit.name = "<eval>";
      unit.source = std::string(arg);
      units.push_back(std::move(unit));
      continue;
    }

    l::module::SourceUnit unit;
    unit.from_memory = false;
    unit.name = std::string(arg);
    units.push_back(std::move(unit));
  }

  if (cmd == CliCommand::Repl) {
    if (!units.empty() || dump_tokens || dump_ast || emit_llvm || emit_obj || shared_lib ||
        !output_path.empty() || saw_eval) {
      std::cerr << "repl does not take source files or emit flags\n";
      return 1;
    }
    return runRepl();
  }

  if (cmd == CliCommand::Eval && !saw_eval) {
    std::cerr << "eval requires a source string: l eval '<code>'\n";
    return 1;
  }

  // `l gen-interface <input.l> -o <output.li>` — public signatures only, no bodies.
  if (cmd == CliCommand::GenInterface) {
    if (units.size() != 1 || units.front().from_memory) {
      std::cerr << "gen-interface requires exactly one input .l file\n";
      return 1;
    }
    if (output_path.empty()) {
      std::cerr << "gen-interface requires -o <output.li>\n";
      return 1;
    }
    if (dump_tokens || dump_ast || emit_llvm || emit_obj || shared_lib || saw_eval) {
      std::cerr << "gen-interface cannot be combined with dump/emit/--shared\n";
      return 1;
    }
    l::diag::DiagnosticEngine diags;
    const std::string input_path = units.front().name;
    const std::string target_mod = fs::path(input_path).stem().string();

    // Parse helpers so types like Array<String> resolve; only `target_mod` is emitted.
    // Do not expand LCMD / comptime — those must not appear in .li.
    std::vector<l::module::SourceUnit> parse_units;
    const fs::path array_l = fs::path(L_SOURCE_DIR) / "stdlib" / "array.l";
    if (fs::exists(array_l) && fs::path(input_path).filename() != "array.l") {
      l::module::SourceUnit helper;
      helper.from_memory = false;
      helper.name = array_l.string();
      parse_units.push_back(std::move(helper));
    }
    parse_units.push_back(units.front());

    auto ast = l::module::loadUnits(parse_units, diags);
    if (!ast || diags.hasErrors()) {
      diags.report(std::cerr);
      return 1;
    }
    const std::string text = l::iface::generateInterfaceFile(*ast, target_mod);
    {
      std::ofstream out(output_path, std::ios::binary);
      if (!out) {
        std::cerr << "cannot write interface file: " << output_path << '\n';
        return 1;
      }
      out << text;
    }
    std::cout << "wrote " << output_path << '\n';
    return 0;
  }

  // `l doc <input.l> -o <output.md>` — Markdown docs for pub API surface.
  if (cmd == CliCommand::Doc) {
    if (units.size() != 1 || units.front().from_memory) {
      std::cerr << "doc requires exactly one input .l file\n";
      return 1;
    }
    if (output_path.empty()) {
      std::cerr << "doc requires -o <output.md>\n";
      return 1;
    }
    if (dump_tokens || dump_ast || emit_llvm || emit_obj || shared_lib || saw_eval) {
      std::cerr << "doc cannot be combined with dump/emit/--shared\n";
      return 1;
    }
    l::diag::DiagnosticEngine diags;
    const std::string input_path = units.front().name;
    const std::string target_mod = fs::path(input_path).stem().string();
    const std::string source_text = readFile(input_path);
    if (source_text.empty() && !fs::exists(input_path)) {
      std::cerr << "cannot read input file: " << input_path << '\n';
      return 1;
    }

    std::vector<l::module::SourceUnit> parse_units;
    const fs::path array_l = fs::path(L_SOURCE_DIR) / "stdlib" / "array.l";
    if (fs::exists(array_l) && fs::path(input_path).filename() != "array.l") {
      l::module::SourceUnit helper;
      helper.from_memory = false;
      helper.name = array_l.string();
      parse_units.push_back(std::move(helper));
    }
    parse_units.push_back(units.front());

    auto ast = l::module::loadUnits(parse_units, diags);
    if (!ast || diags.hasErrors()) {
      diags.report(std::cerr);
      return 1;
    }
    const std::string text =
        l::doc::generateMarkdownDoc(*ast, source_text, target_mod, target_mod);
    {
      std::ofstream out(output_path, std::ios::binary);
      if (!out) {
        std::cerr << "cannot write doc file: " << output_path << '\n';
        return 1;
      }
      out << text;
    }
    std::cout << "wrote " << output_path << '\n';
    return 0;
  }

  if (shared_lib && (dump_tokens || dump_ast || emit_llvm || emit_obj)) {
    std::cerr << "--shared cannot be combined with dump/emit-llvm/emit-obj\n";
    return 1;
  }

  if (cmd == CliCommand::Run && (shared_lib || emit_llvm || emit_obj || dump_tokens || dump_ast)) {
    std::cerr << "run cannot be combined with --shared / dump / emit flags\n";
    return 1;
  }

  if (units.empty()) {
    printUsage(argv[0]);
    return 1;
  }

  // One-file DX: `run` / `eval` pull in core stdlib automatically.
  if (cmd == CliCommand::Run || cmd == CliCommand::Eval) {
    prependCoreStdlib(units);
  }

  l::diag::DiagnosticEngine diags;

  // Primary name for codegen module id / default object path.
  std::string primary_name = units.front().name;
  for (const auto& u : units) {
    if (u.from_memory) {
      primary_name = u.name;
      break;
    }
  }

  // Package manager (Phase 1): optional lconf.l (JIT Configuration-as-Code), then l.toml.
  // Prefer the last disk input (user entry / main.l) so prepending stdlib/*.l does not
  // accidentally resolve the compiler repo's own l.toml / lconf.l.
  l::module::ImportSearchPaths import_paths;
  std::vector<std::string> extra_link_libs;
  {
    fs::path start_dir;
    std::error_code ec;
    for (auto it = units.rbegin(); it != units.rend(); ++it) {
      if (!it->from_memory) {
        start_dir = fs::path(it->name).parent_path();
        if (start_dir.empty()) {
          start_dir = fs::current_path(ec);
        }
        break;
      }
    }
    if (start_dir.empty()) {
      start_dir = fs::current_path(ec);
    }

    // Configuration as Code: JIT-run nearest lconf.l (AbsoluteSymbols → add_dependency).
    const fs::path lconf = l::package::findLconfFile(start_dir);
    if (!lconf.empty()) {
      if (!l::package::executeLconfFile(lconf, opt_level)) {
        return 1;
      }
    }

    const fs::path pkg_root = l::package::findPackageRoot(start_dir);
    if (!pkg_root.empty()) {
      l::package::ResolveResult resolved = l::package::resolveDependencies(pkg_root);
      if (!resolved.ok) {
        std::cerr << resolved.error << '\n';
        return 1;
      }
      import_paths = resolved.import_search_paths;
      for (const fs::path& lib : resolved.link_libs) {
        extra_link_libs.push_back(lib.string());
      }
    }
  }

  if (dump_tokens) {
    for (const l::module::SourceUnit& unit : units) {
      std::string source = unit.source;
      if (!unit.from_memory) {
        source = readFile(unit.name);
        if (source.empty()) {
          std::ifstream probe(unit.name, std::ios::binary);
          if (!probe) {
            l::ast::SourceLocation loc;
            loc.filename = unit.name;
            diags.error(loc, "cannot open source file");
            diags.report(std::cerr);
            return 1;
          }
        }
      }
      l::lexer::Lexer lexer(unit.name, source);
      dumpTokens(lexer.tokenize());
    }
    return 0;
  }

  auto ast = l::compile::compileInputs(units, diags, import_paths);
  if (!ast) {
    diags.report(std::cerr);
    return 1;
  }
  if (diags.hasErrors()) {
    diags.report(std::cerr);
    return 1;
  }

  if (dump_ast) {
    ast->dump(std::cout);
    if (!diags.all().empty()) {
      diags.report(std::cerr);
    }
    return diags.hasErrors() ? 1 : 0;
  }

  l::semantic::Analyzer analyzer;
  analyzer.analyze(*ast, diags);

  std::unique_ptr<l::codegen::Codegen> codegen;
  if (!diags.hasErrors()) {
    codegen = std::make_unique<l::codegen::Codegen>(primary_name, &diags);
    codegen->setOptLevel(opt_level);
    codegen->setSharedMode(shared_lib);
    (void)codegen->emit(*ast);
  }

  if (diags.hasErrors()) {
    diags.report(std::cerr);
    return 1;
  }
  if (diags.hasWarnings()) {
    diags.report(std::cerr);
  }

  if (!codegen) {
    return 1;
  }

  if (emit_llvm) {
    codegen->module().print(llvm::outs(), nullptr);
    return 0;
  }

  if (emit_obj) {
    if (output_path.empty()) {
      if (primary_name == "<stdin>" || primary_name == "<eval>") {
        output_path = "a.o";
      } else {
        output_path = fs::path(primary_name).replace_extension(".o").string();
      }
    }
    if (!codegen->emitObjectFile(output_path)) {
      diags.report(std::cerr);
      return 1;
    }
    std::cerr << "wrote " << output_path << '\n';
    return 0;
  }

  // `eval` / `--eval` without `-o`: pure in-memory ORC JIT (no object / executable).
  if (cmd == CliCommand::Eval && output_path.empty() && !shared_lib) {
    const int rc = l::jit::runModuleMain(codegen->module());
    return rc < 0 ? 1 : rc;
  }

  // Default output path for `build` / `run` when -o is omitted.
  const bool need_link =
      shared_lib || !output_path.empty() || cmd == CliCommand::Build || cmd == CliCommand::Run ||
      (cmd == CliCommand::Eval && !output_path.empty());
  if (need_link && output_path.empty() && !shared_lib && !emit_llvm && !emit_obj) {
#ifdef _WIN32
    const char* ext = ".exe";
#else
    const char* ext = "";
#endif
    if (cmd == CliCommand::Run) {
      output_path = (fs::temp_directory_path() / "l_run_temp.exe").string();
    } else if (primary_name == "<stdin>" || primary_name == "<eval>") {
      output_path = std::string("a") + ext;
    } else {
      output_path = fs::path(primary_name).replace_extension(ext).string();
    }
  }

  if (shared_lib || !output_path.empty()) {
    fs::path out = output_path;
    if (out.empty()) {
#ifdef _WIN32
      const char* ext = shared_lib ? ".dll" : ".exe";
#else
      const char* ext = shared_lib ? ".so" : "";
#endif
      if (primary_name == "<stdin>" || primary_name == "<eval>") {
        out = shared_lib ? fs::path(std::string("libl_ffi") + ext) : fs::path("a.exe");
      } else {
        out = fs::path(primary_name).replace_extension(ext);
      }
    }
    std::string stem = out.stem().string();
    if (stem.empty()) {
      stem = "a";
    }
    const fs::path obj = fs::temp_directory_path() / (stem + ".l.o");
    if (!codegen->emitObjectFile(obj.string())) {
      diags.report(std::cerr);
      return 1;
    }
    const bool ok = shared_lib
                        ? linkSharedLibrary(obj.string(), out.string(), opt_level, extra_link_libs)
                        : linkExecutable(obj.string(), out.string(), opt_level, extra_link_libs);
    std::error_code ec;
    fs::remove(obj, ec);
    if (!ok) {
      return 1;
    }

    if (cmd == CliCommand::Run) {
      const int rc = runExecutable(out.string());
      // Temp run artifacts are cleaned up; explicit -o keeps the binary.
      if (out.filename() == "l_run_temp.exe") {
        fs::remove(out, ec);
      }
      return rc;
    }

    std::cerr << "wrote " << out.string() << '\n';
    return 0;
  }

  std::cerr << "No action requested. Try `l build -o out.exe file.l`, `l run file.l`, "
               "`l eval '...'`, or `l repl`.\n";
  printUsage(argv[0]);
  return 1;
}
