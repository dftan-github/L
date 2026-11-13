#pragma once

#include <string>
#include <utility>
#include <vector>

namespace l::package {

struct ConfigDependency {
  std::string name;
  std::string version;
};

// Reset + configure host state before JIT-running `lconf.l`.
void configHostReset();
void configHostSetBuildMode(const std::string& mode);  // "Debug" | "Release"

const std::vector<ConfigDependency>& configHostDependencies();

// Format: "加载依赖: crypto, network" (empty → "加载依赖: ").
std::string formatLoadedDependenciesLine();

}  // namespace l::package

// C ABI — AbsoluteSymbols targets for ORC JIT (lconf.l calls these).
extern "C" void add_dependency(const char* name, const char* version);
extern "C" const char* get_build_mode();
