#pragma once

#include <string>

#include "ast/ast.hpp"

namespace l::iface {

// Emit a pure interface (.li) text from a parsed Module: pub surface only,
// method/function signatures with no bodies. Strips Directive / extern / imports.
// If `only_module` is non-empty, only emit decls tagged with that module stem
// (so helper deps like array.l can be loaded for parsing without leaking into .li).
std::string generateInterfaceFile(const ast::Module& module,
                                  const std::string& only_module = {});

}  // namespace l::iface
