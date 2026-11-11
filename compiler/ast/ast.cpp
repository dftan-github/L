#include "ast/ast.hpp"
#include "ast/token.hpp"

namespace l::ast {

const char* toString(TokenType type) {
  switch (type) {
    case TokenType::EndOfFile:
      return "EndOfFile";
    case TokenType::Invalid:
      return "Invalid";
    case TokenType::Identifier:
      return "Identifier";
    case TokenType::IntLiteral:
      return "IntLiteral";
    case TokenType::StringLiteral:
      return "StringLiteral";
    case TokenType::KwConst:
      return "KwConst";
    case TokenType::KwConstd:
      return "KwConstd";
    case TokenType::KwFun:
      return "KwFun";
    case TokenType::KwReturn:
      return "KwReturn";
    case TokenType::KwInterface:
      return "KwInterface";
    case TokenType::KwPartial:
      return "KwPartial";
    case TokenType::KwClass:
      return "KwClass";
    case TokenType::TyI32:
      return "TyI32";
    case TokenType::TyI64:
      return "TyI64";
    case TokenType::TyF64:
      return "TyF64";
    case TokenType::TyBool:
      return "TyBool";
    case TokenType::TyVoid:
      return "TyVoid";
    case TokenType::TyString:
      return "TyString";
    case TokenType::Bang:
      return "Bang";
    case TokenType::Equal:
      return "Equal";
    case TokenType::ColonAssign:
      return "ColonAssign";
    case TokenType::Arrow:
      return "Arrow";
    case TokenType::Colon:
      return "Colon";
    case TokenType::Semicolon:
      return "Semicolon";
    case TokenType::Comma:
      return "Comma";
    case TokenType::Dot:
      return "Dot";
    case TokenType::Plus:
      return "Plus";
    case TokenType::Pipe:
      return "Pipe";
    case TokenType::LParen:
      return "LParen";
    case TokenType::RParen:
      return "RParen";
    case TokenType::LBrace:
      return "LBrace";
    case TokenType::RBrace:
      return "RBrace";
    case TokenType::LAngle:
      return "LAngle";
    case TokenType::RAngle:
      return "RAngle";
  }
  return "Unknown";
}

}  // namespace l::ast
