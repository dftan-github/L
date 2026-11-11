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

  // Keywords
  KwConst,
  KwConstd,
  KwFun,
  KwReturn,
  KwInterface,
  KwPartial,
  KwClass,
  KwIf,
  KwElse,
  KwWhile,
  KwFor,
  KwIn,
  KwTrue,
  KwFalse,
  KwHeap,
  KwDefer,
  KwSync,
  KwImport,
  KwPub,

  // Built-in types
  TyI32,
  TyI64,
  TyF64,
  TyBool,
  TyVoid,
  TyString,
  TyPtr,

  // Punctuation / operators
  Bang,          // !
  Equal,         // =
  EqEq,          // ==
  NotEq,         // !=
  ColonAssign,   // :=
  Arrow,         // ->
  Colon,         // :
  Semicolon,     // ;
  Comma,         // ,
  Dot,           // .
  Plus,          // +
  Minus,         // -
  Star,          // *
  Slash,         // /
  AmpAmp,        // &&
  Pipe,          // |
  PipePipe,      // ||
  LParen,        // (
  RParen,        // )
  LBrace,        // {
  RBrace,        // }
  LBracket,      // [
  RBracket,      // ]
  LAngle,        // <
  RAngle,        // >
  LessEq,        // <=
  GreaterEq,     // >=
};

struct Token {
  TokenType type = TokenType::EndOfFile;
  std::string lexeme;
  SourceLocation loc;
};

const char* toString(TokenType type);

}  // namespace l::ast
