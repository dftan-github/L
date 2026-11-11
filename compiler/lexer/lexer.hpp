#pragma once

// Lexer: source text → lightweight Token stream with SourceLocation.

namespace l::lexer {

struct SourceLocation {
  const char* filename = "";
  unsigned line = 1;
  unsigned column = 1;
};

struct Token {
  enum class Kind {
    EndOfFile,
    // Filled in as the lexer grows.
  };

  Kind kind = Kind::EndOfFile;
  SourceLocation loc;
};

class Lexer {
public:
  // Owns nothing; `source` must outlive the Lexer.
  explicit Lexer(const char* filename, const char* source);

  Token next();

private:
  const char* filename_ = "";
  const char* source_ = "";
  const char* cursor_ = "";
  unsigned line_ = 1;
  unsigned column_ = 1;
};

}  // namespace l::lexer
