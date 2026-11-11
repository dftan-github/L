#pragma once

#include <memory>
#include <vector>

#include "ast/token.hpp"

// AST nodes: owned via std::unique_ptr only (no shared_ptr, no owning raw ptr).

namespace l::ast {

struct Node {
  virtual ~Node() = default;
  SourceLocation loc;
};

using NodePtr = std::unique_ptr<Node>;

struct Module {
  std::vector<NodePtr> decls;
};

}  // namespace l::ast
