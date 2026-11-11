#include "lexer/lexer.hpp"

namespace l::lexer {

Lexer::Lexer(const char* filename, const char* source)
    : filename_(filename ? filename : ""),
      source_(source ? source : ""),
      cursor_(source_) {}

Token Lexer::next() {
  Token tok;
  tok.kind = Token::Kind::EndOfFile;
  tok.loc.filename = filename_;
  tok.loc.line = line_;
  tok.loc.column = column_;
  return tok;
}

}  // namespace l::lexer
