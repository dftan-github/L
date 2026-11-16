#include "package/semver.hpp"

#include <cctype>
#include <string>

namespace l::package {
namespace {

std::string trimCopy(const std::string& s) {
  std::size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
    ++b;
  }
  std::size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
    --e;
  }
  return s.substr(b, e - b);
}

// "1.2.3" → {1,2,3}. Stops at the first non-numeric marker (-, +, space).
SemVer parseVersionText(const std::string& text) {
  SemVer v;
  std::string t = trimCopy(text);
  // Strip a leading 'v'.
  if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) {
    t = t.substr(1);
  }
  int parts[3] = {0, 0, 0};
  int idx = 0;
  bool saw_digit = false;
  for (std::size_t i = 0; i < t.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (std::isdigit(c)) {
      parts[idx] = parts[idx] * 10 + (t[i] - '0');
      saw_digit = true;
      continue;
    }
    if (t[i] == '.') {
      if (idx >= 2) {
        break;
      }
      ++idx;
      continue;
    }
    // '-', '+', or junk ends the numeric core.
    break;
  }
  if (!saw_digit) {
    return v;
  }
  v.major = parts[0];
  v.minor = parts[1];
  v.patch = parts[2];
  v.valid = true;
  return v;
}

int cmp(const SemVer& a, const SemVer& b) {
  if (a.major != b.major) {
    return a.major < b.major ? -1 : 1;
  }
  if (a.minor != b.minor) {
    return a.minor < b.minor ? -1 : 1;
  }
  if (a.patch != b.patch) {
    return a.patch < b.patch ? -1 : 1;
  }
  return 0;
}

}  // namespace

SemVer parseSemVer(const std::string& text) { return parseVersionText(text); }

VersionConstraint parseConstraint(const std::string& text) {
  VersionConstraint c;
  std::string t = trimCopy(text);
  if (t.empty() || t == "*" || t == "latest") {
    c.op = VersionConstraint::Op::Any;
    return c;
  }

  // Longest operator first so ">=" is not read as ">".
  struct OpText {
    const char* text;
    VersionConstraint::Op op;
  };
  static const OpText kOps[] = {
      {">=", VersionConstraint::Op::Gte}, {"<=", VersionConstraint::Op::Lte},
      {"~>", VersionConstraint::Op::Tilde}, {">", VersionConstraint::Op::Gt},
      {"<", VersionConstraint::Op::Lt}, {"^", VersionConstraint::Op::Caret},
      {"~", VersionConstraint::Op::Tilde}, {"=", VersionConstraint::Op::Exact},
  };
  for (const OpText& o : kOps) {
    const std::string prefix(o.text);
    if (t.compare(0, prefix.size(), prefix) == 0) {
      c.op = o.op;
      t = trimCopy(t.substr(prefix.size()));
      break;
    }
  }

  c.version = parseVersionText(t);
  if (!c.version.valid) {
    c.valid = false;
  }
  return c;
}

bool satisfies(const SemVer& v, const VersionConstraint& c) {
  if (!c.valid) {
    // A malformed constraint never matches; the resolver reports it separately.
    return false;
  }
  if (c.op == VersionConstraint::Op::Any) {
    return true;
  }
  if (!v.valid) {
    return false;
  }
  const int d = cmp(v, c.version);
  switch (c.op) {
    case VersionConstraint::Op::Exact:
      return d == 0;
    case VersionConstraint::Op::Gt:
      return d > 0;
    case VersionConstraint::Op::Gte:
      return d >= 0;
    case VersionConstraint::Op::Lt:
      return d < 0;
    case VersionConstraint::Op::Lte:
      return d <= 0;
    case VersionConstraint::Op::Caret:
      // ^1.2.3 → >=1.2.3, <2.0.0. ^0.2.3 → >=0.2.3, <0.3.0 (semver caret rule).
      if (d < 0) {
        return false;
      }
      if (c.version.major > 0) {
        return v.major == c.version.major;
      }
      if (c.version.minor > 0) {
        return v.major == 0 && v.minor == c.version.minor;
      }
      return v.major == 0 && v.minor == 0 && v.patch == c.version.patch;
    case VersionConstraint::Op::Tilde:
      // ~1.2.3 → >=1.2.3, <1.3.0. ~1.2 → >=1.2.0, <1.3.0.
      if (d < 0) {
        return false;
      }
      return v.major == c.version.major && v.minor == c.version.minor;
    case VersionConstraint::Op::Any:
      return true;
  }
  return false;
}

std::string constraintToString(const VersionConstraint& c) {
  auto ver = [](const SemVer& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." +
           std::to_string(v.patch);
  };
  switch (c.op) {
    case VersionConstraint::Op::Any:
      return "*";
    case VersionConstraint::Op::Exact:
      return "=" + ver(c.version);
    case VersionConstraint::Op::Caret:
      return "^" + ver(c.version);
    case VersionConstraint::Op::Tilde:
      return "~" + ver(c.version);
    case VersionConstraint::Op::Gt:
      return ">" + ver(c.version);
    case VersionConstraint::Op::Gte:
      return ">=" + ver(c.version);
    case VersionConstraint::Op::Lt:
      return "<" + ver(c.version);
    case VersionConstraint::Op::Lte:
      return "<=" + ver(c.version);
  }
  return "*";
}

}  // namespace l::package
