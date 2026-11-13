#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ast/token.hpp"

namespace l::lexer {

class Lexer {
public:
  // Copies `filename`; `source` is viewed (must outlive tokenize / next calls).
  Lexer(std::string filename, std::string_view source);

  // Lex the entire buffer into tokens, including a trailing EndOfFile.
  // Applies Automatic Semicolon Insertion (ASI) on newlines.
  std::vector<ast::Token> tokenize();

  // Advance one token (used by tokenize and future Parser). Applies ASI.
  ast::Token next();

private:
  ast::SourceLocation location() const;
  char peek(std::size_t offset = 0) const;
  bool atEnd() const;
  void advance();
  void advanceBytes(std::size_t n);

  // Skips spaces/tabs/CR, line/block comments. Sets saw_newline if a '\n' was
  // crossed outside of block comments (line-comment terminator counts).
  void skipWhitespaceAndComments(bool& saw_newline);
  bool trySkipLineComment();
  bool trySkipBlockComment();
  // True when cursor is at `//LCMD_` (directive, not a normal comment).
  bool atLcmdDirective() const;
  ast::Token lexLcmdDirective();

  ast::Token makeToken(ast::TokenType type, std::string lexeme, ast::SourceLocation loc);
  ast::Token lexTokenAtCursor();  // no whitespace skip; cursor on token start
  ast::Token lexIdentifierOrKeyword();
  ast::Token lexNumber();
  ast::Token lexString();
  ast::Token lexPunctuation();

  static bool asiTrigger(ast::TokenType type);
  bool shouldInsertAsiBeforeNext() const;

  // UTF-8 helpers: identifiers may contain Unicode (e.g. Chinese).
  bool decodeUtf8(std::uint32_t& codepoint, std::size_t& byte_len) const;
  static bool isIdentStart(std::uint32_t cp);
  static bool isIdentContinue(std::uint32_t cp);
  static ast::TokenType keywordOrType(std::string_view text);

  std::string filename_;
  std::string_view source_;
  std::size_t index_ = 0;
  std::uint32_t line_ = 1;
  std::uint32_t column_ = 1;

  ast::TokenType prev_type_ = ast::TokenType::EndOfFile;
  bool resumed_after_asi_ = false;  // whitespace already skipped; lex next raw token
};

}  // namespace l::lexer
