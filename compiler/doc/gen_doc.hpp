#pragma once

#include <string>

#include "ast/ast.hpp"

namespace l::doc {

// Emit Markdown API docs for pub fun / pub class / pub interface.
// `source` is the original file text (comments are not in the AST; scraped by line).
// If `only_module` is set, only decls from that module stem are included.
std::string generateMarkdownDoc(const ast::Module& module, const std::string& source,
                                const std::string& only_module = {},
                                const std::string& title = {});

}  // namespace l::doc
