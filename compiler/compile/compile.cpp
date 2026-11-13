#include "compile/compile.hpp"

#include "comptime/expand.hpp"

namespace l::compile {

std::unique_ptr<ast::Module> compileString(const std::string& source,
                                           const std::string& virtualFileName,
                                           diag::DiagnosticEngine& diags) {
  auto ast = module::loadFromString(source, virtualFileName, diags);
  if (!ast) {
    return nullptr;
  }
  (void)comptime::expandLcmdDirectives(*ast, diags);
  return ast;
}

std::unique_ptr<ast::Module> compileInputs(const std::vector<module::SourceUnit>& inputs,
                                           diag::DiagnosticEngine& diags,
                                           const module::ImportSearchPaths& search_paths) {
  auto ast = module::loadUnits(inputs, diags, search_paths);
  if (!ast) {
    return nullptr;
  }
  (void)comptime::expandLcmdDirectives(*ast, diags);
  return ast;
}

}  // namespace l::compile
