#include "lexer/lexer.hpp"

#include <cctype>

namespace l::lexer {

Lexer::Lexer(std::string filename, std::string_view source)
    : filename_(std::move(filename)), source_(source) {
  // Skip UTF-8 BOM so it is not absorbed into the first identifier.
  if (source_.size() >= 3 &&
      static_cast<unsigned char>(source_[0]) == 0xEF &&
      static_cast<unsigned char>(source_[1]) == 0xBB &&
      static_cast<unsigned char>(source_[2]) == 0xBF) {
    index_ = 3;
  }
}

std::vector<ast::Token> Lexer::tokenize() {
  std::vector<ast::Token> tokens;
  for (;;) {
    ast::Token tok = next();
    tokens.push_back(tok);
    if (tok.type == ast::TokenType::EndOfFile) {
      break;
    }
  }
  return tokens;
}

ast::Token Lexer::next() {
  skipWhitespaceAndComments();
  if (atEnd()) {
    return makeToken(ast::TokenType::EndOfFile, "", location());
  }

  const char c = peek();
  if (c == '"') {
    return lexString();
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return lexNumber();
  }

  std::uint32_t cp = 0;
  std::size_t len = 0;
  if (decodeUtf8(cp, len) && isIdentStart(cp)) {
    return lexIdentifierOrKeyword();
  }

  return lexPunctuation();
}

ast::SourceLocation Lexer::location() const {
  return ast::SourceLocation{filename_, line_, column_};
}

char Lexer::peek(std::size_t offset) const {
  const std::size_t i = index_ + offset;
  if (i >= source_.size()) {
    return '\0';
  }
  return source_[i];
}

bool Lexer::atEnd() const {
  return index_ >= source_.size();
}

void Lexer::advance() {
  if (atEnd()) {
    return;
  }
  if (source_[index_] == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  ++index_;
}

void Lexer::advanceBytes(std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    advance();
  }
}

void Lexer::skipWhitespaceAndComments() {
  for (;;) {
    if (atEnd()) {
      return;
    }
    const char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance();
      continue;
    }
    if (trySkipLineComment()) {
      continue;
    }
    if (trySkipBlockComment()) {
      continue;
    }
    return;
  }
}

bool Lexer::trySkipLineComment() {
  if (peek() == '/' && peek(1) == '/') {
    advance();
    advance();
    while (!atEnd() && peek() != '\n') {
      advance();
    }
    return true;
  }
  return false;
}

bool Lexer::trySkipBlockComment() {
  if (peek() == '/' && peek(1) == '*') {
    advance();
    advance();
    // EOF-safe: never read past end if the file ends inside /* ...
    while (!atEnd()) {
      if (peek() == '*' && !atEnd() && peek(1) == '/') {
        advance();
        if (atEnd()) {
          return true;
        }
        advance();
        return true;
      }
      advance();
    }
    return true;
  }
  return false;
}

ast::Token Lexer::makeToken(ast::TokenType type, std::string lexeme, ast::SourceLocation loc) {
  ast::Token tok;
  tok.type = type;
  tok.lexeme = std::move(lexeme);
  tok.loc = std::move(loc);
  return tok;
}

bool Lexer::decodeUtf8(std::uint32_t& codepoint, std::size_t& byte_len) const {
  if (atEnd()) {
    return false;
  }
  const auto b0 = static_cast<unsigned char>(source_[index_]);
  if (b0 < 0x80) {
    codepoint = b0;
    byte_len = 1;
    return true;
  }
  if ((b0 & 0xE0) == 0xC0 && index_ + 1 < source_.size()) {
    const auto b1 = static_cast<unsigned char>(source_[index_ + 1]);
    if ((b1 & 0xC0) != 0x80) {
      return false;
    }
    codepoint = (static_cast<std::uint32_t>(b0 & 0x1F) << 6) | (b1 & 0x3F);
    byte_len = 2;
    return true;
  }
  if ((b0 & 0xF0) == 0xE0 && index_ + 2 < source_.size()) {
    const auto b1 = static_cast<unsigned char>(source_[index_ + 1]);
    const auto b2 = static_cast<unsigned char>(source_[index_ + 2]);
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
      return false;
    }
    codepoint = (static_cast<std::uint32_t>(b0 & 0x0F) << 12) |
                (static_cast<std::uint32_t>(b1 & 0x3F) << 6) | (b2 & 0x3F);
    byte_len = 3;
    return true;
  }
  if ((b0 & 0xF8) == 0xF0 && index_ + 3 < source_.size()) {
    const auto b1 = static_cast<unsigned char>(source_[index_ + 1]);
    const auto b2 = static_cast<unsigned char>(source_[index_ + 2]);
    const auto b3 = static_cast<unsigned char>(source_[index_ + 3]);
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
      return false;
    }
    codepoint = (static_cast<std::uint32_t>(b0 & 0x07) << 18) |
                (static_cast<std::uint32_t>(b1 & 0x3F) << 12) |
                (static_cast<std::uint32_t>(b2 & 0x3F) << 6) | (b3 & 0x3F);
    byte_len = 4;
    return true;
  }
  return false;
}

bool Lexer::isIdentStart(std::uint32_t cp) {
  if (cp < 0x80) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
  }
  // Any non-ASCII code point may start an identifier (Unicode / 中文 names).
  return true;
}

bool Lexer::isIdentContinue(std::uint32_t cp) {
  if (cp < 0x80) {
    return isIdentStart(cp) || (cp >= '0' && cp <= '9');
  }
  return true;
}

ast::TokenType Lexer::keywordOrType(std::string_view text) {
  if (text == "const") {
    return ast::TokenType::KwConst;
  }
  if (text == "constd") {
    return ast::TokenType::KwConstd;
  }
  if (text == "fun") {
    return ast::TokenType::KwFun;
  }
  if (text == "return") {
    return ast::TokenType::KwReturn;
  }
  if (text == "interface") {
    return ast::TokenType::KwInterface;
  }
  if (text == "partial") {
    return ast::TokenType::KwPartial;
  }
  if (text == "class") {
    return ast::TokenType::KwClass;
  }
  if (text == "i32") {
    return ast::TokenType::TyI32;
  }
  if (text == "i64") {
    return ast::TokenType::TyI64;
  }
  if (text == "f64") {
    return ast::TokenType::TyF64;
  }
  if (text == "bool") {
    return ast::TokenType::TyBool;
  }
  if (text == "void") {
    return ast::TokenType::TyVoid;
  }
  if (text == "String") {
    return ast::TokenType::TyString;
  }
  return ast::TokenType::Identifier;
}

ast::Token Lexer::lexIdentifierOrKeyword() {
  const ast::SourceLocation start = location();
  const std::size_t begin = index_;

  std::uint32_t cp = 0;
  std::size_t len = 0;
  while (!atEnd() && decodeUtf8(cp, len) && isIdentContinue(cp)) {
    // Column advances one display column per Unicode scalar (Phase 1).
    index_ += len;
    ++column_;
  }

  const std::string text(source_.substr(begin, index_ - begin));
  return makeToken(keywordOrType(text), text, start);
}

ast::Token Lexer::lexNumber() {
  const ast::SourceLocation start = location();
  const std::size_t begin = index_;
  while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
    advance();
  }
  const std::string text(source_.substr(begin, index_ - begin));
  return makeToken(ast::TokenType::IntLiteral, text, start);
}

ast::Token Lexer::lexString() {
  const ast::SourceLocation start = location();
  advance();  // opening "
  const std::size_t begin = index_;
  while (!atEnd() && peek() != '"') {
    if (peek() == '\\' && peek(1) != '\0') {
      advance();
      advance();
      continue;
    }
    if (peek() == '\n') {
      break;
    }
    advance();
  }
  const std::string text(source_.substr(begin, index_ - begin));
  if (!atEnd() && peek() == '"') {
    advance();
  }
  return makeToken(ast::TokenType::StringLiteral, text, start);
}

ast::Token Lexer::lexPunctuation() {
  const ast::SourceLocation start = location();
  const char c = peek();
  const char n = peek(1);

  if (c == ':' && n == '=') {
    advance();
    advance();
    return makeToken(ast::TokenType::ColonAssign, ":=", start);
  }
  if (c == '-' && n == '>') {
    advance();
    advance();
    return makeToken(ast::TokenType::Arrow, "->", start);
  }

  advance();
  switch (c) {
    case '!':
      return makeToken(ast::TokenType::Bang, "!", start);
    case '=':
      return makeToken(ast::TokenType::Equal, "=", start);
    case ':':
      return makeToken(ast::TokenType::Colon, ":", start);
    case ';':
      return makeToken(ast::TokenType::Semicolon, ";", start);
    case ',':
      return makeToken(ast::TokenType::Comma, ",", start);
    case '.':
      return makeToken(ast::TokenType::Dot, ".", start);
    case '+':
      return makeToken(ast::TokenType::Plus, "+", start);
    case '|':
      return makeToken(ast::TokenType::Pipe, "|", start);
    case '(':
      return makeToken(ast::TokenType::LParen, "(", start);
    case ')':
      return makeToken(ast::TokenType::RParen, ")", start);
    case '{':
      return makeToken(ast::TokenType::LBrace, "{", start);
    case '}':
      return makeToken(ast::TokenType::RBrace, "}", start);
    case '<':
      return makeToken(ast::TokenType::LAngle, "<", start);
    case '>':
      return makeToken(ast::TokenType::RAngle, ">", start);
    default: {
      std::string lexeme(1, c);
      return makeToken(ast::TokenType::Invalid, lexeme, start);
    }
  }
}

}  // namespace l::lexer
