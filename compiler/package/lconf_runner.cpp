#include "package/lconf_runner.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

#include "codegen/codegen.hpp"
#include "compile/compile.hpp"
#include "diagnostic/diagnostic.hpp"
#include "jit/orc_jit.hpp"
#include "package/config_host.hpp"
#include "semantic/semantic.hpp"

namespace fs = std::filesystem;

namespace l::package {
namespace {

std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Injected before user lconf.l so add_dependency / get_build_mode / l_str_eq resolve.
constexpr const char* kLconfHostPrefix = R"(
extern module l_config {
  fun add_dependency(ptr name, ptr version) -> void
  fun get_build_mode() -> ptr
}
extern module c_io {
  fun l_str_eq(ptr a, ptr b) -> i32
}
)";

}  // namespace

fs::path findLconfFile(const fs::path& start_dir) {
  std::error_code ec;
  fs::path cur = fs::weakly_canonical(start_dir, ec);
  if (ec) {
    cur = fs::absolute(start_dir, ec);
  }
  while (true) {
    const fs::path candidate = cur / "lconf.l";
    if (fs::exists(candidate)) {
      return candidate;
    }
    fs::path parent = cur.parent_path();
    if (parent == cur) {
      break;
    }
    cur = parent;
  }
  return {};
}

bool executeLconfFile(const fs::path& lconf_path, unsigned opt_level) {
  const std::string body = readFile(lconf_path);
  if (body.empty()) {
    std::cerr << "[ERROR] " << lconf_path.string() << ":1:1 - cannot read lconf.l\n";
    return false;
  }

  configHostReset();
  // Match compiler -O / --release: O0 → Debug, otherwise Release.
  configHostSetBuildMode(opt_level == 0 ? "Debug" : "Release");

  const std::string full = std::string(kLconfHostPrefix) + "\n" + body;
  const std::string virt = lconf_path.string();

  l::diag::DiagnosticEngine diags;
  auto ast = l::compile::compileString(full, virt, diags);
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

  l::codegen::Codegen codegen(virt, &diags);
  codegen.setOptLevel(opt_level);
  if (!codegen.emit(*ast) || diags.hasErrors()) {
    diags.report(std::cerr);
    return false;
  }

  const int rc = l::jit::runModuleMain(codegen.module());
  if (rc < 0) {
    return false;
  }

  std::cout << formatLoadedDependenciesLine() << '\n';
  return true;
}

}  // namespace l::package
