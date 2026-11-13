#include "package/toml.hpp"

#include <fstream>
#include <sstream>

namespace l::package {
namespace {

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.pop_back();
  }
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  return s.substr(i);
}

// Strip optional surrounding quotes from a TOML string value.
std::string unquote(std::string s) {
  s = trim(s);
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                        (s.front() == '\'' && s.back() == '\''))) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

bool isSection(const std::string& line, const char* name) {
  return line == std::string("[") + name + "]";
}

}  // namespace

bool parseTomlFile(const std::string& path, Manifest& out, std::string& error_out) {
  out = Manifest{};
  out.source_path = path;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error_out = "cannot open " + path;
    return false;
  }

  enum class Section { None, Package, Dependencies };
  Section section = Section::None;
  std::string line;
  while (std::getline(in, line)) {
    // Drop comments.
    const auto hash = line.find('#');
    if (hash != std::string::npos) {
      line = line.substr(0, hash);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      if (isSection(line, "package")) {
        section = Section::Package;
      } else if (isSection(line, "dependencies")) {
        section = Section::Dependencies;
      } else {
        section = Section::None;
      }
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = trim(line.substr(0, eq));
    const std::string val = unquote(line.substr(eq + 1));
    if (section == Section::Package) {
      if (key == "name") {
        out.name = val;
      } else if (key == "version") {
        out.version = val;
      }
    } else if (section == Section::Dependencies) {
      if (!key.empty()) {
        Dependency d;
        d.name = key;
        d.version = val;
        out.dependencies.push_back(std::move(d));
      }
    }
  }
  return true;
}

}  // namespace l::package
