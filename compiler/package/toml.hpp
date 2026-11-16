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

// One resolved entry in `l.lock`: the exact version a dependency resolved to.
struct LockEntry {
  std::string name;
  std::string version;  // exact "MAJOR.MINOR.PATCH" as found on disk
};

struct Lockfile {
  std::vector<LockEntry> packages;
};

// Minimal TOML reader for [package] name/version and [dependencies] string pairs.
// Returns false on I/O failure; parse issues leave fields empty / partial.
bool parseTomlFile(const std::string& path, Manifest& out, std::string& error_out);

// Read `l.lock` next to the manifest. Missing file → empty lockfile, ok = true.
bool parseLockfile(const std::string& path, Lockfile& out, std::string& error_out);

// Serialize a lockfile in a stable, diff-friendly form.
std::string lockfileToString(const Lockfile& lock);

bool writeLockfile(const std::string& path, const Lockfile& lock);

}  // namespace l::package
