#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ast/ast.hpp"
#include "ast/token.hpp"
#include "codegen/codegen.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "semantic/semantic.hpp"

#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#ifndef L_BUILD_DIR
#define L_BUILD_DIR "."
#endif

#ifndef L_CLANG_PATH
#define L_CLANG_PATH "clang++"
#endif

namespace fs = std::filesystem;

namespace {

void printUsage(const char* argv0) {
  std::cerr << "Usage:\n"
            << "  " << argv0 << " [--dump-tokens|--dump-ast|--emit-llvm] <source.l>...\n"
            << "  " << argv0 << " -o <output.exe> <source.l>...\n"
            << "  " << argv0 << " --emit-obj -o <output.o> <source.l>...\n";
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

bool loadSource(const std::string& path, std::string& out) {
  out = readFile(path);
  std::ifstream probe(path, std::ios::binary);
  return static_cast<bool>(probe);
}

void dumpTokens(const std::vector<l::ast::Token>& tokens) {
  for (const l::ast::Token& tok : tokens) {
    std::cout << l::ast::toString(tok.type) << " \"" << tok.lexeme << "\" @ "
              << tok.loc.filename << ':' << tok.loc.line << ':' << tok.loc.column
              << '\n';
  }
}

void collectTypeNames(const l::ast::Module& module,
                      std::vector<std::string>& classes,
                      std::vector<std::string>& interfaces) {
  for (const l::ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    if (stmt->kind() == l::ast::StmtKind::ClassDecl) {
      classes.push_back(static_cast<const l::ast::ClassDeclStmt&>(*stmt).name);
    } else if (stmt->kind() == l::ast::StmtKind::InterfaceDecl) {
      interfaces.push_back(static_cast<const l::ast::InterfaceDeclStmt&>(*stmt).name);
    }
  }
}

std::unique_ptr<l::ast::Module> parseSources(const std::vector<std::string>& paths) {
  auto merged = std::make_unique<l::ast::Module>();
  std::vector<std::string> known_classes;
  std::vector<std::string> known_interfaces;

  for (const std::string& path : paths) {
    std::string source;
    if (!loadSource(path, source)) {
      std::cerr << "Cannot open source file: " << path << '\n';
      return nullptr;
    }
    l::lexer::Lexer lexer(path, source);
    std::vector<l::ast::Token> tokens = lexer.tokenize();
    l::parser::Parser parser(std::move(tokens));
    for (const std::string& c : known_classes) {
      parser.registerClassName(c);
    }
    for (const std::string& i : known_interfaces) {
      parser.registerInterfaceName(i);
    }
    std::unique_ptr<l::ast::Module> module = parser.parseModule();
    for (const std::string& diag : parser.diagnostics()) {
      std::cerr << diag << '\n';
    }
    if (parser.hadError() || !module) {
      return nullptr;
    }
    collectTypeNames(*module, known_classes, known_interfaces);
    for (l::ast::StmtPtr& stmt : module->stmts) {
      merged->stmts.push_back(std::move(stmt));
    }
  }
  return merged;
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

bool linkExecutable(const std::string& object_path, const std::string& exe_path) {
  const std::string clang = clangPath();
  const std::string runtime = runtimeLibPath();
  if (!fs::exists(runtime)) {
    std::cerr << "runtime library not found: " << runtime
              << "\nBuild l_runtime first (cmake --build --target l_runtime).\n";
    return false;
  }

  {
    std::error_code ec;
    fs::create_directories(fs::path(exe_path).parent_path(), ec);
  }

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
    bat << '"' << clang << "\" \"" << object_path << "\" \"" << runtime << "\" -o \"" << exe_path
        << "\"\r\n";
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
  std::vector<llvm::StringRef> args = {clang, object_path, runtime, "-o", exe_path};
  std::string err;
  const int rc = llvm::sys::ExecuteAndWait(clang, args, std::nullopt, {}, 0, 0, &err);
#endif
  if (rc != 0) {
    std::cerr << "link failed (exit " << rc << "): " << err << '\n';
    std::cerr << "Tried link of " << object_path << " + " << runtime << " -> " << exe_path
              << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  bool dump_tokens = false;
  bool dump_ast = false;
  bool emit_llvm = false;
  bool emit_obj = false;
  std::string output_path;
  std::vector<std::string> source_paths;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
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
    if (arg == "-o") {
      if (i + 1 >= argc) {
        std::cerr << "-o requires an output path\n";
        return 1;
      }
      output_path = argv[++i];
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    }
    source_paths.emplace_back(arg);
  }

  if (source_paths.empty()) {
    printUsage(argv[0]);
    return 1;
  }

  if (dump_tokens) {
    for (const std::string& path : source_paths) {
      std::string source;
      if (!loadSource(path, source)) {
        std::cerr << "Cannot open source file: " << path << '\n';
        return 1;
      }
      l::lexer::Lexer lexer(path, source);
      dumpTokens(lexer.tokenize());
    }
    return 0;
  }

  auto ast = parseSources(source_paths);
  if (!ast) {
    return 1;
  }

  if (dump_ast) {
    ast->dump(std::cout);
    return 0;
  }

  l::semantic::Analyzer analyzer;
  if (!analyzer.analyze(*ast)) {
    return 1;
  }

  l::codegen::Codegen codegen(source_paths.front());
  if (!codegen.emit(*ast)) {
    return 1;
  }

  if (emit_llvm) {
    codegen.module().print(llvm::outs(), nullptr);
    return 0;
  }

  if (emit_obj) {
    if (output_path.empty()) {
      output_path = fs::path(source_paths.front()).replace_extension(".o").string();
    }
    if (!codegen.emitObjectFile(output_path)) {
      return 1;
    }
    std::cerr << "wrote " << output_path << '\n';
    return 0;
  }

  if (!output_path.empty()) {
    const fs::path exe = output_path;
    const fs::path obj = fs::temp_directory_path() /
                         (exe.stem().string() + ".l.o");
    if (!codegen.emitObjectFile(obj.string())) {
      return 1;
    }
    if (!linkExecutable(obj.string(), exe.string())) {
      return 1;
    }
    std::error_code ec;
    fs::remove(obj, ec);
    std::cerr << "wrote " << exe.string() << '\n';
    return 0;
  }

  std::cerr << "No action requested. Try --emit-llvm, --emit-obj -o <file.o>, or -o <file.exe>.\n";
  printUsage(argv[0]);
  return 1;
}
