#include "package/resolve.hpp"

#include <unordered_map>
#include <unordered_set>

namespace l::package {
namespace fs = std::filesystem;

namespace {

fs::path findInterfaceFile(const fs::path& pkg_root, const std::string& name) {
  const fs::path li = pkg_root / (name + ".li");
  if (fs::exists(li)) {
    return li;
  }
  const fs::path lfile = pkg_root / (name + ".l");
  if (fs::exists(lfile)) {
    return lfile;
  }
  // Also accept lib-style layout: vendor/foo/src/foo.li
  const fs::path li2 = pkg_root / "src" / (name + ".li");
  if (fs::exists(li2)) {
    return li2;
  }
  return {};
}

fs::path findStaticLib(const fs::path& pkg_root, const std::string& name) {
  const std::vector<fs::path> candidates = {
      pkg_root / ("lib" + name + ".a"),
      pkg_root / (name + ".a"),
      pkg_root / ("lib" + name + ".lib"),
      pkg_root / (name + ".lib"),
  };
  for (const fs::path& c : candidates) {
    if (fs::exists(c)) {
      return c;
    }
  }
  return {};
}

struct CycleCtx {
  std::unordered_map<std::string, Manifest> manifests;  // name → parsed
  std::unordered_set<std::string> visiting;
  std::unordered_set<std::string> visited;
  std::string cycle_a;
  std::string cycle_b;
  bool cycle = false;
};

bool loadManifestFor(CycleCtx& ctx, const fs::path& vendor_root, const std::string& name,
                     Manifest& out, std::string& err) {
  auto it = ctx.manifests.find(name);
  if (it != ctx.manifests.end()) {
    out = it->second;
    return true;
  }
  const fs::path toml = vendor_root / name / "l.toml";
  if (!fs::exists(toml)) {
    // Synthetic manifest when vendor package has no l.toml (leaf package).
    Manifest m;
    m.name = name;
    m.version = "";
    m.source_path = toml.string();
    ctx.manifests[name] = m;
    out = m;
    return true;
  }
  if (!parseTomlFile(toml.string(), out, err)) {
    return false;
  }
  if (out.name.empty()) {
    out.name = name;
  }
  ctx.manifests[name] = out;
  return true;
}

// DFS cycle detection over dependency names under vendor_root.
bool walkDeps(CycleCtx& ctx, const fs::path& vendor_root, const std::string& name,
              std::string& err) {
  if (ctx.cycle) {
    return false;
  }
  if (ctx.visited.count(name) != 0) {
    return true;
  }
  if (ctx.visiting.count(name) != 0) {
    // Cycle edge closes on `name`; find a peer still on the stack — report pair.
    ctx.cycle = true;
    ctx.cycle_b = name;
    // Prefer reporting the other endpoint as the immediate back-edge source when known.
    return false;
  }
  ctx.visiting.insert(name);

  Manifest man;
  if (!loadManifestFor(ctx, vendor_root, name, man, err)) {
    ctx.visiting.erase(name);
    return false;
  }
  for (const Dependency& d : man.dependencies) {
    if (ctx.visiting.count(d.name) != 0) {
      ctx.cycle = true;
      ctx.cycle_a = name;
      ctx.cycle_b = d.name;
      ctx.visiting.erase(name);
      return false;
    }
    if (!walkDeps(ctx, vendor_root, d.name, err)) {
      if (ctx.cycle && ctx.cycle_a.empty()) {
        ctx.cycle_a = name;
      }
      ctx.visiting.erase(name);
      return false;
    }
  }

  ctx.visiting.erase(name);
  ctx.visited.insert(name);
  return true;
}

}  // namespace

fs::path findPackageRoot(const fs::path& start_dir) {
  std::error_code ec;
  fs::path cur = fs::weakly_canonical(start_dir, ec);
  if (ec) {
    cur = fs::absolute(start_dir, ec);
  }
  while (true) {
    if (fs::exists(cur / "l.toml")) {
      return cur;
    }
    fs::path parent = cur.parent_path();
    if (parent == cur) {
      break;
    }
    cur = parent;
  }
  return {};
}

ResolveResult resolveDependencies(const fs::path& package_root) {
  ResolveResult result;
  result.ok = false;
  if (package_root.empty() || !fs::exists(package_root / "l.toml")) {
    // No manifest → nothing to resolve (not an error).
    result.ok = true;
    return result;
  }

  std::string err;
  if (!parseTomlFile((package_root / "l.toml").string(), result.root_manifest, err)) {
    result.error = "[ERROR] " + err;
    return result;
  }

  const fs::path vendor = package_root / "vendor";
  CycleCtx ctx;
  ctx.manifests[result.root_manifest.name.empty() ? std::string("<root>")
                                                  : result.root_manifest.name] =
      result.root_manifest;

  for (const Dependency& d : result.root_manifest.dependencies) {
    if (!walkDeps(ctx, vendor, d.name, err)) {
      if (ctx.cycle) {
        std::string a = ctx.cycle_a.empty() ? d.name : ctx.cycle_a;
        std::string b = ctx.cycle_b.empty() ? d.name : ctx.cycle_b;
        result.error =
            "[ERROR] Circular dependency detected between '" + a + "' and '" + b + "'";
      } else {
        result.error = "[ERROR] " + (err.empty() ? std::string("dependency resolve failed") : err);
      }
      return result;
    }
  }

  // Collect resolved packages in dependency order (visited set order is fine for Phase 1).
  for (const Dependency& d : result.root_manifest.dependencies) {
    // Include transitive packages discovered during walk.
    (void)d;
  }
  for (const auto& kv : ctx.manifests) {
    const std::string& name = kv.first;
    if (name == "<root>" || name == result.root_manifest.name) {
      continue;
    }
    const fs::path root = vendor / name;
    if (!fs::exists(root)) {
      result.error = "[ERROR] missing vendor package '" + name + "' at " + root.string();
      return result;
    }
    ResolvedPackage pkg;
    pkg.name = name;
    pkg.version = kv.second.version;
    pkg.root = root;
    pkg.interface_file = findInterfaceFile(root, name);
    pkg.static_lib = findStaticLib(root, name);
    if (pkg.interface_file.empty()) {
      result.error =
          "[ERROR] package '" + name + "' has no interface file (" + name + ".li)";
      return result;
    }
    result.packages.push_back(pkg);
    result.import_search_paths.push_back(root);
    if (!pkg.static_lib.empty()) {
      result.link_libs.push_back(pkg.static_lib);
    }
  }

  // Also search vendor/ itself and package root for imports.
  if (fs::exists(vendor)) {
    result.import_search_paths.push_back(vendor);
  }
  result.import_search_paths.push_back(package_root);
  result.ok = true;
  return result;
}

}  // namespace l::package
