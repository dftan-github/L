#include "module/loader.hpp"

#include <charconv>
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

fs::path canonicalOrSelf(const fs::path& path) {
  std::error_code ec;
  fs::path abs = fs::weakly_canonical(path, ec);
  if (ec) {
    abs = fs::absolute(path, ec);
  }
  return abs.empty() ? path : abs;
}

// Import-chain segments of a file (`net/http/server.l` → {net, http}). Empty for
// flat modules (`mod_a.l`), which stay reachable by their bare stem. The chain
// starts right below the nearest directory holding a sibling module
// (`tests/cases/ns/net/net.l` → `net/http/server.l` is `{net, http}`).
std::vector<std::string> dirScopeSegments(const std::string& abs_path, const fs::path& base) {
  std::error_code ec;
  const fs::path file = canonicalOrSelf(fs::path(abs_path));
  const fs::path root = canonicalOrSelf(base);
  std::vector<std::string> parts;
  fs::path cursor = file.parent_path();
  for (;;) {
    if (cursor.empty() || cursor == cursor.root_path()) {
      return {};
    }
    const std::string name = cursor.filename().generic_string();
    if (!name.empty()) {
      parts.push_back(name);
    }
    if (fs::exists(cursor / (name + ".l"), ec)) {
      std::reverse(parts.begin(), parts.end());
      return parts;
    }
    if (cursor == root) {
      return {};
    }
    cursor = cursor.parent_path();
  }
}

std::string dirScopeOf(const std::string& abs_path, const fs::path& base) {
  const std::vector<std::string> parts = dirScopeSegments(abs_path, base);
  std::string chain;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      chain += "::";
    }
    chain += parts[i];
  }
  return chain;
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

// Module name of a file as seen by `name::call`. For a namespaced import
// (`import net::http::server`, resolved to net/http/server.l) the dotted path
// from `root` becomes the namespace, so the module is reachable as
// `net::http::server::fn`. Files outside `root` fall back to the bare stem.
std::string namespaceFor(const fs::path& file, const fs::path& root) {
  std::error_code ec;
  const fs::path rel = fs::relative(file, root, ec);
  if (ec || rel.empty() || rel.native().rfind(fs::path("..").native(), 0) == 0) {
    return moduleStemFor(file.string());
  }
  fs::path parent = rel.parent_path();
  if (parent.empty() || parent == ".") {
    return moduleStemFor(file.string());
  }
  std::string ns = parent.generic_string();
  for (char& c : ns) {
    if (c == '/') {
      c = '.';
    }
  }
  return ns;
}

// `a.b.c` → `a::b::c` for module-qualified lookup keys.
std::string dottedToScope(const std::string& dotted) {
  std::string out = dotted;
  for (char& c : out) {
    if (c == '.') {
      c = ':';
    }
  }
  return out;
}

// Last `::`-separated segment of a scope key (`a::b::C` → `C`).
std::string scopeLeaf(const std::string& scope) {
  const std::size_t pos = scope.rfind(':');
  return pos == std::string::npos ? scope : scope.substr(pos + 1);
}

// Flat-module name for a hierarchical import (`import net::http::server` →
// `net_http_server`), matching the file convention `a_b_c.l`.
std::string joinSegments(const std::vector<std::string>& segs) {
  std::string out;
  for (std::size_t i = 0; i < segs.size(); ++i) {
    if (i != 0) {
      out += "::";
    }
    out += segs[i];
  }
  return out;
}

// `::` chain with `_` separators for flat module files (`a::b::c` → `a_b_c`).
std::string flatSegments(const std::vector<std::string>& segs) {
  std::string out;
  for (std::size_t i = 0; i < segs.size(); ++i) {
    if (i != 0) {
      out += "_";
    }
    out += segs[i];
  }
  return out;
}

// A `use` alias names a type when its leaf starts with an uppercase letter.
// That is the codebase convention for classes/interfaces (`Server`, `User`);
// everything else aliases a module namespace and calls through `Alias::fn()`.
bool isTypeName(const std::string& leaf) {
  return !leaf.empty() && leaf.front() >= 'A' && leaf.front() <= 'Z';
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
    } else         if (stmt->kind() == ast::StmtKind::InterfaceDecl) {
      interfaces.push_back(static_cast<const ast::InterfaceDeclStmt&>(*stmt).name);
    }
  }
}

// `namespace::leaf` (`net/http/server.l` under `net/http`) — the full module name
// as imported (`import net::http::server`). Falls back to the bare stem.
std::string qualifiedModuleName(
    const std::unordered_set<std::string>& names, const std::string& leaf) {
  for (const std::string& n : names) {
    // A candidate (`net::http::server`) ends with `::leaf` or is exactly `leaf`.
    if (n == leaf) {
      continue;
    }
    const std::string suffix = "::" + leaf;
    if (n.size() > suffix.size() &&
        n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return n;
    }
  }
  return leaf;
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
  // Root of the entry file: namespaced module names are relative to it.
  fs::path root;
  // Stdlib generic type names are known up-front: an entry file may `import` a
  // stdlib module that uses `Array<String>` / `Map<V>` before the module is
  // parsed, and `<` would otherwise lex as a comparison operator.
  std::vector<std::string> known_classes = {"String", "Array", "Optional", "Map",
                                            "Result", "Variant", "CPropertys"};
  std::vector<std::string> known_interfaces;
  // Every module name loaded so far — lets a bare-stem tag (`server`) be upgraded
  // to the hierarchical name it was imported under (`net::http::server`).
  std::unordered_set<std::string> module_paths;

  // Feed every type name discovered so far into a fresh parser.
  void knowTypeNames(l::parser::Parser& parser) const {
    for (const std::string& c : known_classes) {
      parser.registerClassName(c);
    }
    for (const std::string& i : known_interfaces) {
      parser.registerInterfaceName(i);
    }
  }

  // Resolve import path relative to import_base and package search paths.
  // Tries .l / .li and vendor/<name>/<name>.li layouts.
  fs::path resolveImportPath(const std::string& imp_path, const fs::path& import_base) const {
    std::vector<fs::path> bases;
    bases.push_back(import_base);
    // A source unit's own directory is an implicit import base.
    std::error_code sec;
    const fs::path canonical_dir = fs::weakly_canonical(import_base, sec);
    if (!sec) {
      bases.push_back(canonical_dir.parent_path());
    }
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
    knowTypeNames(parser);

    std::unique_ptr<ast::Module> module = parser.parseModule();
    if (!module) {
      loading.erase(key);
      return false;
    }

    const bool is_li = fs::path(display_name).extension() == ".li";
    markInterfaceExterns(*module, is_li);
    // Hierarchical names this module imports (`import a::b::c` keeps the chain).
    std::vector<std::string> imported_now;

    // Namespaced modules (`import net::http::server` → net/http/server.l) get a
    // dotted module name so their symbols live under an isolated `a.b.c` scope.
  const std::string stem_name = moduleStemFor(display_name);
  // Flat (filesystem root) modules keep their bare stem; once the directory chain
  // is known below, nested files are re-tagged with their namespace.
  const std::string mod_name = stem_name;
    module_paths.insert(mod_name);
    collectTypeNames(*module, known_classes, known_interfaces);

    for (ast::StmtPtr& stmt : module->stmts) {
      if (!stmt) {
        continue;
      }
      if (stmt->kind() == ast::StmtKind::Import) {
        const auto& imp = static_cast<const ast::ImportStmt&>(*stmt);
        fs::path child = resolveImportPath(imp.path, import_base);
        if (child.empty() && !imp.path_segments.empty()) {
          // `import a::b::c` may also name a flat module file `a_b_c.l`
          // (directory hierarchy requires a `<name>.l` marker above it).
          child = resolveImportPath(flatSegments(imp.path_segments), import_base);
        }
        if (child.empty()) {
          ast::SourceLocation loc = imp.loc;
          if (diags) {
            diags->error(loc, "cannot resolve import '" + imp.path + "'");
          }
          loading.erase(key);
          return false;
        }
        child = canonicalOrSelf(child);
        if (!imp.path_segments.empty()) {
          import_names[child.string()] = joinSegments(imp.path_segments);
          imported_now.push_back(joinSegments(imp.path_segments));
          out.imported_modules.push_back(joinSegments(imp.path_segments));
        } else {
          // Unqualified import: keep both spellings playable as a namespace
          // (`m::fn` for `import m` and for `import m.l`).
          imported_now.push_back(fs::path(imp.path).stem().string());
          out.imported_modules.push_back(fs::path(imp.path).stem().string());
          out.imported_modules.push_back(flatSegments(imp.path_segments));
        }
        if (!loadFile(child, out)) {
          loading.erase(key);
          return false;
        }
        // Types of the freshly loaded module become known to every later module,
        // so `<` in `Array<Car>` still lexes as generic brackets.
        knowTypeNames(parser);
        // A hierarchical import names the module by its full chain
        // (`import net::http::server` → `net::http::server::fn`).
        if (!imp.path_segments.empty()) {
          module_paths.insert(joinSegments(imp.path_segments));
        }
        // Unqualified imports must land on a file with the same stem
        // (`import net_server` → `net_server.l`); vendor layouts may differ.
        const std::string wanted_stem =
            imp.path_segments.empty() ? fs::path(imp.path).stem().string()
                                      : imp.path_segments.back();
        if (child.parent_path().filename() != "vendor" && wanted_stem != child.stem()) {
          if (diags) {
            diags->error(imp.loc, "cannot resolve import '" + imp.path + "'");
          }
          loading.erase(key);
          return false;
        }
        continue;
      }
      if (stmt->kind() == ast::StmtKind::Use) {
        // `use a::b::Symbol;` is pure compile-time aliasing: the resolver
        // rewrites `Symbol::x()` to `a::b::Symbol::x()` before any IR is built.
        // The statement itself never leaves the loader.
        const auto& use = static_cast<const ast::UseStmt&>(*stmt);
        ast::UseAlias alias;
        alias.alias = use.alias.empty() ? use.path.back() : use.alias;
        // A `use` target that is itself an imported module (`use net::http::server`)
        // names that module's directory namespace (`net::http`): aliased calls
        // resolve like the `.` receiver path (`Alias::fn` → `net::http::fn`).
        // Anything else is a type alias (`use net::http::Server` → `Server`).
        const std::string joined = joinSegments(use.path);
        std::string module_leaf;
        for (const std::string& m : out.imported_modules) {
          if (m == joined && m.find("::") != std::string::npos) {
            module_leaf = scopeLeaf(m);
            break;
          }
        }
        bool module_target = !module_leaf.empty();
        if (module_target) {
          // Keep the module's own leaf: a namespace alias is just a shorter name
          // for the target module (`http::greet` → `net::http::server::greet`).
          alias.scope = joined;
        }
        if (!module_target) {
          // Multi-segment (`use a::b::C`) or single-segment (`use Cfg`).
          // In the latter case the import must be of the module that owns the
          // type: `use net_server::Config` names the file `net_server.l` itself.
          alias.scope = use.path.size() > 1 ? joinSegments(use.path) : use.path.back();
          alias.is_type = isTypeName(use.path.back());
        }
        out.usages.push_back(alias);
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

  // Re-tag with the hierarchical name the module was imported under (`import
  // net::http::server`) plus its directory chain, so symbols are looked up as
  // `net::http::server::fn` and same-stem modules in other trees stay isolated.
  std::string final_name = mod_name;
  const auto imported = import_names.find(key);
        if (imported != import_names.end()) {
    final_name = imported->second;
  } else {
    const std::string chain = dirScopeOf(key, import_base.parent_path());
    if (!chain.empty()) {
      final_name = chain + "::" + stem_name;
    }
  }
  module_paths.insert(final_name);
  if (final_name != mod_name) {
    std::vector<std::string> segs;
    std::size_t start = 0;
    for (;;) {
      const std::size_t pos = final_name.find("::", start);
      segs.push_back(final_name.substr(start, pos == std::string::npos
                                                  ? std::string::npos
                                                  : pos - start));
      if (pos == std::string::npos) {
        break;
      }
      start = pos + 2;
    }
  for (const std::string& entry : dirScopeSegments(key, import_base.parent_path())) {
    (void)entry;
  }
    path_by_qualified[final_name] = segs;
    for (ast::StmtPtr& stmt : out.stmts) {
      if (stmt) {
        tagDeclModule(*stmt, final_name);
      }
    }
    for (auto& h : out.init_hooks) {
      tagDeclModule(*h, final_name);
    }
    for (auto& h : out.deinit_hooks) {
      tagDeclModule(*h, final_name);
    }
  }

  loading.erase(key);
  loaded.insert(key);
  return true;
}

  // Segments of every hierarchical module name (`net::http::server` → {net, http, server}).
  std::unordered_map<std::string, std::vector<std::string>> path_by_qualified;
  // `import a::b::c` → absolute path of c.l maps to `a::b::c`.
  std::unordered_map<std::string, std::string> import_names;

  bool loadFile(const fs::path& path, ast::Module& out) {    std::error_code ec;
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
  }};

}  // namespace

std::unique_ptr<ast::Module> loadEntry(const std::string& entry_path,
                                       diag::DiagnosticEngine& diags,
                                       const ImportSearchPaths& search_paths) {
  Loader loader;
  loader.diags = &diags;
  loader.search_paths = search_paths;
  std::error_code ec;
  const fs::path entry = fs::weakly_canonical(fs::path(entry_path), ec);
  loader.root = (ec ? fs::path(entry_path) : entry).parent_path();
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
  std::error_code root_ec;
  loader.root = fs::current_path(root_ec);
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
  loader.root = import_base;
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
  loader.root = cwd;
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
