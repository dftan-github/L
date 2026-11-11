#include "module/loader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

namespace fs = std::filesystem;

namespace l::module {
namespace {

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string moduleStem(const fs::path& path) {
  return path.stem().string();
}

void tagDeclModule(ast::Stmt& stmt, const std::string& mod) {
  switch (stmt.kind()) {
    case ast::StmtKind::FuncDecl:
      static_cast<ast::FuncDeclStmt&>(stmt).module_name = mod;
      break;
    case ast::StmtKind::ClassDecl: {
      auto& cls = static_cast<ast::ClassDeclStmt&>(stmt);
      cls.module_name = mod;
      for (auto& method : cls.methods) {
        if (method) {
          method->module_name = mod;
        }
      }
      break;
    }
    case ast::StmtKind::InterfaceDecl:
      static_cast<ast::InterfaceDeclStmt&>(stmt).module_name = mod;
      break;
    default:
      break;
  }
}

void collectTypeNames(const ast::Module& module,
                      std::vector<std::string>& classes,
                      std::vector<std::string>& interfaces) {
  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    if (stmt->kind() == ast::StmtKind::ClassDecl) {
      classes.push_back(static_cast<const ast::ClassDeclStmt&>(*stmt).name);
    } else if (stmt->kind() == ast::StmtKind::InterfaceDecl) {
      interfaces.push_back(static_cast<const ast::InterfaceDeclStmt&>(*stmt).name);
    }
  }
}

struct Loader {
  std::unordered_set<std::string> loading;   // absolute paths in recursion stack
  std::unordered_set<std::string> loaded;    // fully loaded absolute paths
  std::vector<std::string> known_classes;
  std::vector<std::string> known_interfaces;

  bool loadFile(const fs::path& path, std::vector<ast::StmtPtr>& out) {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(path, ec);
    if (ec) {
      abs = fs::absolute(path, ec);
    }
    const std::string key = abs.string();

    if (loading.count(key) != 0) {
      std::cerr << "import cycle detected involving: " << key << '\n';
      return false;
    }
    if (loaded.count(key) != 0) {
      return true;  // already merged once
    }

    std::string source = readFile(key);
    if (source.empty() && !fs::exists(abs)) {
      std::cerr << "Cannot open module: " << path.string() << '\n';
      return false;
    }

    loading.insert(key);
    l::lexer::Lexer lexer(key, source);
    std::vector<ast::Token> tokens = lexer.tokenize();
    l::parser::Parser parser(std::move(tokens));
    for (const std::string& c : known_classes) {
      parser.registerClassName(c);
    }
    for (const std::string& i : known_interfaces) {
      parser.registerInterfaceName(i);
    }

    std::unique_ptr<ast::Module> module = parser.parseModule();
    for (const std::string& diag : parser.diagnostics()) {
      std::cerr << diag << '\n';
    }
    if (parser.hadError() || !module) {
      loading.erase(key);
      return false;
    }

    const std::string mod_name = moduleStem(abs);
    collectTypeNames(*module, known_classes, known_interfaces);

    // Depth-first: expand imports first, then keep local declarations.
    for (ast::StmtPtr& stmt : module->stmts) {
      if (!stmt) {
        continue;
      }
      if (stmt->kind() == ast::StmtKind::Import) {
        const auto& imp = static_cast<const ast::ImportStmt&>(*stmt);
        fs::path child = abs.parent_path() / imp.path;
        if (!loadFile(child, out)) {
          loading.erase(key);
          return false;
        }
        continue;
      }
      tagDeclModule(*stmt, mod_name);
      out.push_back(std::move(stmt));
    }

    loading.erase(key);
    loaded.insert(key);
    return true;
  }
};

}  // namespace

std::unique_ptr<ast::Module> loadEntry(const std::string& entry_path) {
  Loader loader;
  auto merged = std::make_unique<ast::Module>();
  if (!loader.loadFile(fs::path(entry_path), merged->stmts)) {
    return nullptr;
  }
  return merged;
}

std::unique_ptr<ast::Module> loadEntries(const std::vector<std::string>& entry_paths) {
  Loader loader;
  auto merged = std::make_unique<ast::Module>();
  for (const std::string& path : entry_paths) {
    if (!loader.loadFile(fs::path(path), merged->stmts)) {
      return nullptr;
    }
  }
  return merged;
}

}  // namespace l::module
