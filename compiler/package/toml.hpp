#pragma once

#include <string>
#include <vector>

namespace l::package {

struct Dependency {
  std::string name;
  std::string version;
};

struct Manifest {
  std::string name;
  std::string version;
  std::vector<Dependency> dependencies;
  std::string source_path;  // path of the l.toml that was parsed
};

// Minimal TOML reader for [package] name/version and [dependencies] string pairs.
// Returns false on I/O failure; parse issues leave fields empty / partial.
bool parseTomlFile(const std::string& path, Manifest& out, std::string& error_out);

}  // namespace l::package
