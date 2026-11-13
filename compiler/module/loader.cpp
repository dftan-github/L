#include "module/loader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
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

std::string moduleStemFor(const std::string& name) {
  if (name == "<stdin>") {
    return "stdin";
  }
  if (name == "<eval>") {
    return "eval";
  }
  return fs::path(name).stem().string();
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

void collectTypeNames(const ast::Module& module, std::vector<std::string>& classes,
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

// Mark signature-only decls in .li files as C ABI externs (unmangled ExternalLinkage).
void markInterfaceExterns(ast::Module& module, bool is_li) {
  if (!is_li) {
    return;
  }
  for (ast::StmtPtr& stmt : module.stmts) {
    if (!stmt || stmt->kind() != ast::StmtKind::FuncDecl) {
      continue;
    }
    auto& fn = static_cast<ast::FuncDeclStmt&>(*stmt);
    if (fn.body.empty()) {
      fn.is_extern = true;
    }
  }
}

struct Loader {
  diag::DiagnosticEngine* diags = nullptr;
  ImportSearchPaths search_paths;
  std::unordered_set<std::string> loading;
  std::unordered_set<std::string> loaded;
  std::vector<std::string> known_classes;
  std::vector<std::string> known_interfaces;

  // Resolve import path relative to import_base and package search paths.
  // Tries .l / .li and vendor/<name>/<name>.li layouts.
  fs::path resolveImportPath(const std::string& imp_path, const fs::path& import_base) const {
    std::vector<fs::path> bases;
    bases.push_back(import_base);
    for (const fs::path& p : search_paths) {
      bases.push_back(p);
    }

    auto try_one = [](const fs::path& candidate) -> fs::path {
      if (!candidate.empty() && fs::exists(candidate)) {
        return candidate;
      }
      return {};
    };

    // Exact path as written (may already include .l / .li).
    for (const fs::path& base : bases) {
      fs::path hit = try_one(base / imp_path);
      if (!hit.empty()) {
        return hit;
      }
    }

    // Stem without extension: try .l then .li, plus vendor/name/name.li.
    fs::path as_path(imp_path);
    const std::string stem = as_path.stem().string();
    const bool has_ext = as_path.has_extension();
    for (const fs::path& base : bases) {
      if (!has_ext) {
        fs::path hit = try_one(base / (stem + ".l"));
        if (!hit.empty()) {
          return hit;
        }
        hit = try_one(base / (stem + ".li"));
        if (!hit.empty()) {
          return hit;
        }
      }
      // vendor/<stem>/<stem>.li under this base (base may already be vendor/).
      fs::path hit = try_one(base / stem / (stem + ".li"));
      if (!hit.empty()) {
        return hit;
      }
      hit = try_one(base / stem / (stem + ".l"));
      if (!hit.empty()) {
        return hit;
      }
      hit = try_one(base / "vendor" / stem / (stem + ".li"));
      if (!hit.empty()) {
        return hit;
      }
      hit = try_one(base / "vendor" / stem / (stem + ".l"));
      if (!hit.empty()) {
        return hit;
      }
    }
    return {};
  }

  bool loadBuffer(const std::string& source, const std::string& display_name,
                  const fs::path& import_base, ast::Module& out) {
    const std::string key = display_name;

    if (loading.count(key) != 0) {
      ast::SourceLocation loc;
      loc.filename = display_name;
      if (diags) {
        diags->error(loc, "import cycle detected");
      }
      return false;
    }
    if (loaded.count(key) != 0) {
      return true;
    }

    loading.insert(key);
    l::lexer::Lexer lexer(display_name, source);
    std::vector<ast::Token> tokens = lexer.tokenize();
    l::parser::Parser parser(std::move(tokens), diags);
    for (const std::string& c : known_classes) {
      parser.registerClassName(c);
    }
    for (const std::string& i : known_interfaces) {
      parser.registerInterfaceName(i);
    }

    std::unique_ptr<ast::Module> module = parser.parseModule();
    if (!module) {
      loading.erase(key);
      return false;
    }

    const bool is_li = fs::path(display_name).extension() == ".li";
    markInterfaceExterns(*module, is_li);

    const std::string mod_name = moduleStemFor(display_name);
    collectTypeNames(*module, known_classes, known_interfaces);

    for (ast::StmtPtr& stmt : module->stmts) {
      if (!stmt) {
        continue;
      }
      if (stmt->kind() == ast::StmtKind::Import) {
        const auto& imp = static_cast<const ast::ImportStmt&>(*stmt);
        fs::path child = resolveImportPath(imp.path, import_base);
        if (child.empty()) {
          ast::SourceLocation loc = imp.loc;
          if (diags) {
            diags->error(loc, "cannot resolve import '" + imp.path + "'");
          }
          loading.erase(key);
          return false;
        }
        out.imported_modules.push_back(fs::path(imp.path).stem().string());
        if (!loadFile(child, out)) {
          loading.erase(key);
          return false;
        }
        continue;
      }
      tagDeclModule(*stmt, mod_name);
      out.stmts.push_back(std::move(stmt));
    }

    for (auto& h : module->init_hooks) {
      if (!h) {
        continue;
      }
      tagDeclModule(*h, mod_name);
      out.init_hooks.push_back(std::move(h));
    }
    for (auto& h : module->deinit_hooks) {
      if (!h) {
        continue;
      }
      tagDeclModule(*h, mod_name);
      out.deinit_hooks.push_back(std::move(h));
    }

    loading.erase(key);
    loaded.insert(key);
    return true;
  }

  bool loadFile(const fs::path& path, ast::Module& out) {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(path, ec);
    if (ec) {
      abs = fs::absolute(path, ec);
    }
    const std::string key = abs.string();

    if (loading.count(key) != 0) {
      ast::SourceLocation loc;
      loc.filename = key;
      if (diags) {
        diags->error(loc, "import cycle detected");
      }
      return false;
    }
    if (loaded.count(key) != 0) {
      return true;
    }

    std::string source = readFile(key);
    if (source.empty() && !fs::exists(abs)) {
      ast::SourceLocation loc;
      loc.filename = path.string();
      if (diags) {
        diags->error(loc, "cannot open module");
      }
      return false;
    }

    return loadBuffer(source, key, abs.parent_path(), out);
  }
};

}  // namespace

std::unique_ptr<ast::Module> loadEntry(const std::string& entry_path,
                                       diag::DiagnosticEngine& diags,
                                       const ImportSearchPaths& search_paths) {
  Loader loader;
  loader.diags = &diags;
  loader.search_paths = search_paths;
  auto merged = std::make_unique<ast::Module>();
  if (!loader.loadFile(fs::path(entry_path), *merged)) {
    return nullptr;
  }
  return merged;
}

std::unique_ptr<ast::Module> loadEntries(const std::vector<std::string>& entry_paths,
                                         diag::DiagnosticEngine& diags,
                                         const ImportSearchPaths& search_paths) {
  Loader loader;
  loader.diags = &diags;
  loader.search_paths = search_paths;
  auto merged = std::make_unique<ast::Module>();
  for (const std::string& path : entry_paths) {
    if (!loader.loadFile(fs::path(path), *merged)) {
      return nullptr;
    }
  }
  return merged;
}

std::unique_ptr<ast::Module> loadFromString(const std::string& source,
                                            const std::string& virtual_filename,
                                            diag::DiagnosticEngine& diags,
                                            const ImportSearchPaths& search_paths) {
  Loader loader;
  loader.diags = &diags;
  loader.search_paths = search_paths;
  auto merged = std::make_unique<ast::Module>();
  std::error_code ec;
  const fs::path import_base = fs::current_path(ec);
  if (!loader.loadBuffer(source, virtual_filename, import_base, *merged)) {
    return nullptr;
  }
  return merged;
}

std::unique_ptr<ast::Module> loadUnits(const std::vector<SourceUnit>& units,
                                       diag::DiagnosticEngine& diags,
                                       const ImportSearchPaths& search_paths) {
  Loader loader;
  loader.diags = &diags;
  loader.search_paths = search_paths;
  auto merged = std::make_unique<ast::Module>();
  std::error_code ec;
  const fs::path cwd = fs::current_path(ec);
  for (const SourceUnit& unit : units) {
    if (unit.from_memory) {
      if (!loader.loadBuffer(unit.source, unit.name, cwd, *merged)) {
        return nullptr;
      }
    } else {
      if (!loader.loadFile(fs::path(unit.name), *merged)) {
        return nullptr;
      }
    }
  }
  return merged;
}

}  // namespace l::module
