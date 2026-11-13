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

bool Lexer::asiTrigger(ast::TokenType type) {
  switch (type) {
    case ast::TokenType::Identifier:
    case ast::TokenType::IntLiteral:
    case ast::TokenType::FloatLiteral:
    case ast::TokenType::StringLiteral:
    case ast::TokenType::KwTrue:   // `return true` / `return false`
    case ast::TokenType::KwFalse:
    case ast::TokenType::RParen:
    case ast::TokenType::RBrace:
    case ast::TokenType::RBracket:  // `arr := [1, 2, 3]` / `return data_[i]`
    case ast::TokenType::Bang:
    case ast::TokenType::Question:  // `safe_div(...)?`
    case ast::TokenType::PlusPlus:    // `a!++` / statement form
    case ast::TokenType::MinusMinus:  // `a!--`
    // Type tokens end interface method signatures: `fun f() -> i32`
    case ast::TokenType::TyI32:
    case ast::TokenType::TyI64:
    case ast::TokenType::TyF64:
    case ast::TokenType::TyBool:
    case ast::TokenType::TyVoid:
    case ast::TokenType::TyString:
    case ast::TokenType::TyPtr:
      return true;
    default:
      return false;
  }
}

bool Lexer::shouldInsertAsiBeforeNext() const {
  // RBrace is listed as an ASI trigger, but L block statements (if/while/sync/
  // defer/fun/class) are not semicolon-terminated, and the parser has no empty
  // statement. Inserting after '}' breaks `}\nprint(...)` and `}\nif`.
  // Trailing-block expression statements must use an explicit ';' after '}'.
  if (prev_type_ == ast::TokenType::RBrace) {
    return false;
  }
  if (!asiTrigger(prev_type_)) {
    return false;
  }
  if (atEnd()) {
    return true;
  }
  if (peek() == ';') {
    return false;
  }
  return true;
}

std::vector<ast::Token> Lexer::tokenize() {
  std::vector<ast::Token> tokens;
  prev_type_ = ast::TokenType::EndOfFile;
  resumed_after_asi_ = false;
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
  if (!resumed_after_asi_) {
    bool saw_newline = false;
    skipWhitespaceAndComments(saw_newline);
    if (saw_newline && shouldInsertAsiBeforeNext()) {
      const ast::SourceLocation loc = location();
      resumed_after_asi_ = true;
      prev_type_ = ast::TokenType::Semicolon;
      return makeToken(ast::TokenType::Semicolon, ";", loc);
    }
  } else {
    resumed_after_asi_ = false;
  }

  if (atEnd()) {
    prev_type_ = ast::TokenType::EndOfFile;
    return makeToken(ast::TokenType::EndOfFile, "", location());
  }

  ast::Token tok = lexTokenAtCursor();
  prev_type_ = tok.type;
  return tok;
}

ast::Token Lexer::lexTokenAtCursor() {
  if (atLcmdDirective()) {
    return lexLcmdDirective();
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

void Lexer::skipWhitespaceAndComments(bool& saw_newline) {
  saw_newline = false;
  for (;;) {
    if (atEnd()) {
      return;
    }
    const char c = peek();
    if (c == ' ' || c == '\t' || c == '\r') {
      advance();
      continue;
    }
    if (c == '\n') {
      saw_newline = true;
      advance();
      continue;
    }
    if (trySkipLineComment()) {
      // Line comment body stopped before '\n'; the following newline is ASI-relevant.
      continue;
    }
    if (trySkipBlockComment()) {
      // Newlines inside block comments do not trigger ASI.
      continue;
    }
    return;
  }
}

bool Lexer::atLcmdDirective() const {
  // //LCMD_
  return peek() == '/' && peek(1) == '/' && peek(2) == 'L' && peek(3) == 'C' &&
         peek(4) == 'M' && peek(5) == 'D' && peek(6) == '_';
}

bool Lexer::trySkipLineComment() {
  if (peek() == '/' && peek(1) == '/') {
    if (atLcmdDirective()) {
      return false;  // leave for lexLcmdDirective
    }
    advance();
    advance();
    while (!atEnd() && peek() != '\n') {
      advance();
    }
    return true;
  }
  // `# …` line comments (doc / scripting style); scraped by `l doc`.
  if (peek() == '#') {
    advance();
    while (!atEnd() && peek() != '\n') {
      advance();
    }
    return true;
  }
  return false;
}

ast::Token Lexer::lexLcmdDirective() {
  const ast::SourceLocation loc = location();
  // Consume `//LCMD_`
  advanceBytes(7);
  std::string body;
  while (!atEnd() && peek() != '\n') {
    body.push_back(peek());
    advance();
  }
  // Trim trailing CR / spaces.
  while (!body.empty() && (body.back() == '\r' || body.back() == ' ' || body.back() == '\t')) {
    body.pop_back();
  }
  return makeToken(ast::TokenType::LcmdDirective, std::move(body), loc);
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
  if (text == "if") {
    return ast::TokenType::KwIf;
  }
  if (text == "else") {
    return ast::TokenType::KwElse;
  }
  if (text == "match") {
    return ast::TokenType::KwMatch;
  }
  if (text == "as") {
    return ast::TokenType::KwAs;
  }
  if (text == "while") {
    return ast::TokenType::KwWhile;
  }
  if (text == "for") {
    return ast::TokenType::KwFor;
  }
  if (text == "in") {
    return ast::TokenType::KwIn;
  }
  if (text == "true") {
    return ast::TokenType::KwTrue;
  }
  if (text == "false") {
    return ast::TokenType::KwFalse;
  }
  if (text == "heap") {
    return ast::TokenType::KwHeap;
  }
  if (text == "arena") {
    return ast::TokenType::KwArena;
  }
  if (text == "defer") {
    return ast::TokenType::KwDefer;
  }
  if (text == "sync") {
    return ast::TokenType::KwSync;
  }
  if (text == "comptime") {
    return ast::TokenType::KwComptime;
  }
  if (text == "spawn") {
    return ast::TokenType::KwSpawn;
  }
  if (text == "import") {
    return ast::TokenType::KwImport;
  }
  if (text == "pub") {
    return ast::TokenType::KwPub;
  }
  if (text == "static") {
    return ast::TokenType::KwStatic;
  }
  if (text == "extern") {
    return ast::TokenType::KwExtern;
  }
  if (text == "module") {
    return ast::TokenType::KwModule;
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
  if (text == "ptr") {
    return ast::TokenType::TyPtr;
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
  const ast::TokenType ty = keywordOrType(text);
  // Rule 70/73: identifiers must not end with `_` (physical fields are compiler-hidden).
  // Bare `_` remains valid (match wildcard). Compile-time context macros are exceptions.
  if (ty == ast::TokenType::Identifier && text.size() > 1 && text.back() == '_') {
    if (text != "__FILE__" && text != "__FUNC__" && text != "__LINE__") {
      return makeToken(ast::TokenType::Invalid, text, start);
    }
  }
  return makeToken(ty, text, start);
}

ast::Token Lexer::lexNumber() {
  const ast::SourceLocation start = location();
  const std::size_t begin = index_;
  while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
    advance();
  }
  // Float: digits '.' digits  (e.g. 3.14)
  if (!atEnd() && peek() == '.' &&
      std::isdigit(static_cast<unsigned char>(peek(1)))) {
    advance();  // '.'
    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
      advance();
    }
    const std::string text(source_.substr(begin, index_ - begin));
    return makeToken(ast::TokenType::FloatLiteral, text, start);
  }
  const std::string text(source_.substr(begin, index_ - begin));
  return makeToken(ast::TokenType::IntLiteral, text, start);
}

ast::Token Lexer::lexString() {
  const ast::SourceLocation start = location();
  advance();  // opening "
  std::string text;
  text.reserve(32);
  while (!atEnd() && peek() != '"') {
    if (peek() == '\\' && peek(1) != '\0') {
      advance();  // consume '\'
      const char esc = peek();
      advance();
      switch (esc) {
        case 'n':
          text.push_back('\n');
          break;
        case 'r':
          text.push_back('\r');
          break;
        case 't':
          text.push_back('\t');
          break;
        case '"':
          text.push_back('"');
          break;
        case '\\':
          text.push_back('\\');
          break;
        default:
          // Keep unknown escapes as raw `\<c>` so interpolation can still see `\{`.
          text.push_back('\\');
          text.push_back(esc);
          break;
      }
      continue;
    }
    if (peek() == '\n') {
      break;  // unclosed string
    }
    text.push_back(peek());
    advance();
  }
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
  if (c == '=' && n == '>') {
    advance();
    advance();
    return makeToken(ast::TokenType::FatArrow, "=>", start);
  }
  if (c == '=' && n == '=') {
    advance();
    advance();
    return makeToken(ast::TokenType::EqEq, "==", start);
  }
  if (c == '!' && n == '=') {
    advance();
    advance();
    return makeToken(ast::TokenType::NotEq, "!=", start);
  }
  if (c == '<' && n == '=') {
    advance();
    advance();
    return makeToken(ast::TokenType::LessEq, "<=", start);
  }
  if (c == '>' && n == '=') {
    advance();
    advance();
    return makeToken(ast::TokenType::GreaterEq, ">=", start);
  }
  if (c == '&' && n == '&') {
    advance();
    advance();
    return makeToken(ast::TokenType::AmpAmp, "&&", start);
  }
  if (c == '&') {
    advance();
    return makeToken(ast::TokenType::Amp, "&", start);
  }
  if (c == '|' && n == '|') {
    advance();
    advance();
    return makeToken(ast::TokenType::PipePipe, "||", start);
  }
  if (c == '+' && n == '+') {
    advance();
    advance();
    return makeToken(ast::TokenType::PlusPlus, "++", start);
  }
  if (c == '-' && n == '-') {
    advance();
    advance();
    return makeToken(ast::TokenType::MinusMinus, "--", start);
  }
  if (c == '+' && n == '=') {
    advance();
    advance();
    return makeToken(ast::TokenType::PlusEqual, "+=", start);
  }
  if (c == '-' && n == '=') {
    advance();
    advance();
    return makeToken(ast::TokenType::MinusEqual, "-=", start);
  }
  if (c == '*' && n == '=') {
    advance();
    advance();
    return makeToken(ast::TokenType::StarEqual, "*=", start);
  }
  if (c == '/' && n == '=') {
    advance();
    advance();
    return makeToken(ast::TokenType::SlashEqual, "/=", start);
  }
  if (c == '.' && n == '.') {
    advance();
    advance();
    return makeToken(ast::TokenType::DotDot, "..", start);
  }

  advance();
  switch (c) {
    case '!':
      return makeToken(ast::TokenType::Bang, "!", start);
    case '?':
      return makeToken(ast::TokenType::Question, "?", start);
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
    case '-':
      return makeToken(ast::TokenType::Minus, "-", start);
    case '*':
      return makeToken(ast::TokenType::Star, "*", start);
    case '/':
      return makeToken(ast::TokenType::Slash, "/", start);
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
    case '[':
      return makeToken(ast::TokenType::LBracket, "[", start);
    case ']':
      return makeToken(ast::TokenType::RBracket, "]", start);
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
