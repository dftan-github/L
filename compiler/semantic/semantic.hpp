#pragma once

#include "ast/ast.hpp"

namespace l::semantic {

class Analyzer {
public:
  // Merge partial classes, check mutability/`sync`, resolve names.
  // Returns false if fatal diagnostics were emitted.
  bool analyze(ast::Module& module);
};

}  // namespace l::semantic
