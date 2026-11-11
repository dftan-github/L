#pragma once

#include <cstdint>
#include <string>

// Source locations and tokens live in the AST layer so Parser/Semantic share one type.

namespace l::ast {

struct SourceLocation {
  std::string filename;
  std::uint32_t line = 1;
  std::uint32_t column = 1;
};

enum class TokenType {
  // Special
  EndOfFile,
  Invalid,

  // Literals & names
  Identifier,
  IntLiteral,
  StringLiteral,

  // Keywords (README §18 golden case)
  KwConst,
  KwConstd,
  KwFun,
  KwReturn,
  KwInterface,
  KwPartial,
  KwClass,

  // Phase 1 built-in types (+ String name for golden/stdlib surface)
  TyI32,
  TyI64,
  TyF64,
  TyBool,
  TyVoid,
  TyString,

  // Punctuation / operators
  Bang,          // !
  Equal,         // =
  ColonAssign,   // :=
  Arrow,         // ->
  Colon,         // :
  Semicolon,     // ;
  Comma,         // ,
  Dot,           // .
  Plus,          // +
  Pipe,          // |
  LParen,        // (
  RParen,        // )
  LBrace,        // {
  RBrace,        // }
  LAngle,        // <
  RAngle,        // >
};

struct Token {
  TokenType type = TokenType::EndOfFile;
  std::string lexeme;
  SourceLocation loc;
};

// Human-readable name for --dump-tokens and diagnostics.
const char* toString(TokenType type);

}  // namespace l::ast
