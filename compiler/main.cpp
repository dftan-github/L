#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ast/token.hpp"
#include "lexer/lexer.hpp"

namespace {

void printUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " [--dump-tokens] <source.l>\n";
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void dumpTokens(const std::vector<l::ast::Token>& tokens) {
  for (const l::ast::Token& tok : tokens) {
    std::cout << l::ast::toString(tok.type) << " \"" << tok.lexeme << "\" @ "
              << tok.loc.filename << ':' << tok.loc.line << ':' << tok.loc.column
              << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  bool dump_tokens = false;
  std::string source_path;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--dump-tokens") {
      dump_tokens = true;
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    }
    if (!source_path.empty()) {
      std::cerr << "Unexpected argument: " << arg << '\n';
      printUsage(argv[0]);
      return 1;
    }
    source_path = std::string(arg);
  }

  if (source_path.empty()) {
    printUsage(argv[0]);
    return 1;
  }

  const std::string source = readFile(source_path);
  if (source.empty()) {
    // Distinguish missing file from empty file via stream state above.
    std::ifstream probe(source_path, std::ios::binary);
    if (!probe) {
      std::cerr << "Cannot open source file: " << source_path << '\n';
      return 1;
    }
  }

  if (dump_tokens) {
    l::lexer::Lexer lexer(source_path, source);
    dumpTokens(lexer.tokenize());
    return 0;
  }

  std::cerr << "No action requested. Try --dump-tokens.\n";
  printUsage(argv[0]);
  return 1;
}
