#pragma once

#include <filesystem>
#include <string>

namespace l::package {

// Walk upward from `start_dir` for `lconf.l`; empty if not found.
std::filesystem::path findLconfFile(const std::filesystem::path& start_dir);

// Parse + codegen + ORC-JIT execute `lconf.l`. Host hooks fill configHostDependencies().
// Returns false on compile/JIT failure (diagnostics already printed to stderr).
bool executeLconfFile(const std::filesystem::path& lconf_path, unsigned opt_level);

}  // namespace l::package
