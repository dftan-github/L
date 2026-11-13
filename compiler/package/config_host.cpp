#include "package/config_host.hpp"

#include <mutex>
#include <sstream>

namespace {

std::mutex g_mu;
std::vector<l::package::ConfigDependency> g_deps;
std::string g_build_mode = "Debug";

}  // namespace

namespace l::package {

void configHostReset() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_deps.clear();
  g_build_mode = "Debug";
}

void configHostSetBuildMode(const std::string& mode) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_build_mode = mode.empty() ? "Debug" : mode;
}

const std::vector<ConfigDependency>& configHostDependencies() {
  return g_deps;
}

std::string formatLoadedDependenciesLine() {
  std::lock_guard<std::mutex> lock(g_mu);
  std::ostringstream oss;
  oss << "加载依赖: ";
  for (size_t i = 0; i < g_deps.size(); ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << g_deps[i].name;
  }
  return oss.str();
}

}  // namespace l::package

extern "C" void add_dependency(const char* name, const char* version) {
  std::lock_guard<std::mutex> lock(g_mu);
  l::package::ConfigDependency d;
  d.name = name ? name : "";
  d.version = version ? version : "";
  g_deps.push_back(std::move(d));
}

extern "C" const char* get_build_mode() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_build_mode.c_str();
}
