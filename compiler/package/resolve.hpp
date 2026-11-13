#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "package/toml.hpp"

namespace l::package {

struct ResolvedPackage {
  std::string name;
  std::string version;
  std::filesystem::path root;            // vendor/<name>
  std::filesystem::path interface_file;  // .li (preferred) or .l
  std::filesystem::path static_lib;      // .a / .lib (may be empty if missing)
};

struct ResolveResult {
  bool ok = false;
  std::string error;  // human message (cycle / missing), already formatted for stderr
  Manifest root_manifest;
  std::vector<ResolvedPackage> packages;
  // Directories that the module loader should search for `import name`.
  std::vector<std::filesystem::path> import_search_paths;
  std::vector<std::filesystem::path> link_libs;
};

// Walk upward from `start_dir` looking for l.toml; returns that directory (or empty).
std::filesystem::path findPackageRoot(const std::filesystem::path& start_dir);

// Resolve [dependencies] under package_root/vendor/, detect circular deps.
// On cycle: error = "[ERROR] Circular dependency detected between 'A' and 'B'"
ResolveResult resolveDependencies(const std::filesystem::path& package_root);

}  // namespace l::package
