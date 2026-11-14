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
  FloatLiteral,
  StringLiteral,
  // `//LCMD_cmd(args)` — lexeme is `cmd(args)` (everything after LCMD_).
  LcmdDirective,

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
  KwMatch,
  KwAs,
  KwWhile,
  KwFor,
  KwIn,
  KwTrue,
  KwFalse,
  KwHeap,
  KwArena,
  KwDefer,
  KwSync,
  KwSpawn,
  KwComptime,
  KwImport,
  KwPub,
  KwStatic,
  KwExtern,
  KwModule,
  KwDelegate,

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
  Question,      // ?  (Result error propagation)
  QuestionQuestion,  // ?? (Optional null-coalescing)
  Equal,         // =
  EqEq,          // ==
  NotEq,         // !=
  ColonAssign,   // :=
  Arrow,         // ->
  FatArrow,      // =>
  Colon,         // :
  Semicolon,     // ;
  Comma,         // ,
  Dot,           // .
  DotDot,        // ..
  Plus,          // +
  Minus,         // -
  Star,          // *
  Slash,         // /
  PlusEqual,     // +=
  MinusEqual,    // -=
  StarEqual,     // *=
  SlashEqual,    // /=
  PlusPlus,      // ++
  MinusMinus,    // --
  AmpAmp,        // &&
  Amp,           // &
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
