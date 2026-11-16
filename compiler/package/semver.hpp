#pragma once

#include <string>

namespace l::package {

// Parsed semantic version: MAJOR.MINOR.PATCH. Pre-release / build metadata are
// ignored (Phase 1): "1.2.3-beta+build" parses as 1.2.3.
struct SemVer {
  int major = 0;
  int minor = 0;
  int patch = 0;
  bool valid = false;
};

// Parse "1", "1.2", "1.2.3" (each numeric part optional beyond major).
// Returns a SemVer with valid=false when the text is not a version at all.
SemVer parseSemVer(const std::string& text);

// A constraint such as "^1.2", "~1.2.3", ">=1.0.0", "1.2.3", "*".
struct VersionConstraint {
  enum class Op { Any, Exact, Caret, Tilde, Gte, Gt, Lte, Lt };
  Op op = Op::Any;
  SemVer version;
  bool valid = true;
};

// Parse a constraint string. An empty string or "*" means Any.
VersionConstraint parseConstraint(const std::string& text);

// True when `version` satisfies `constraint`.
bool satisfies(const SemVer& version, const VersionConstraint& constraint);

// Human-readable constraint for lockfile / error output.
std::string constraintToString(const VersionConstraint& c);

}  // namespace l::package
