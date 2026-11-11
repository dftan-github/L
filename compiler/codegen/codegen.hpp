#pragma once

#include <string>

#include "ast/ast.hpp"

namespace l::codegen {

class CodeGenerator {
public:
  // Lower AST → LLVM IR (IRBuilder). Returns IR text for early phases.
  std::string emit(ast::Module& module);
};

}  // namespace l::codegen
