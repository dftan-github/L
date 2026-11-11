#include "ast/ast.hpp"

#include <ostream>

namespace l::ast {
namespace {

void pad(std::ostream& out, int indent) {
  for (int i = 0; i < indent; ++i) {
    out << "  ";
  }
}

void dumpStmts(std::ostream& out, int indent, const std::vector<StmtPtr>& stmts) {
  for (const StmtPtr& stmt : stmts) {
    if (stmt) {
      stmt->dump(out, indent);
    }
  }
}

}  // namespace

const char* toString(TokenType type) {
  switch (type) {
    case TokenType::EndOfFile: return "EndOfFile";
    case TokenType::Invalid: return "Invalid";
    case TokenType::Identifier: return "Identifier";
    case TokenType::IntLiteral: return "IntLiteral";
    case TokenType::StringLiteral: return "StringLiteral";
    case TokenType::KwConst: return "KwConst";
    case TokenType::KwConstd: return "KwConstd";
    case TokenType::KwFun: return "KwFun";
    case TokenType::KwReturn: return "KwReturn";
    case TokenType::KwInterface: return "KwInterface";
    case TokenType::KwPartial: return "KwPartial";
    case TokenType::KwClass: return "KwClass";
    case TokenType::KwIf: return "KwIf";
    case TokenType::KwElse: return "KwElse";
    case TokenType::KwWhile: return "KwWhile";
    case TokenType::KwTrue: return "KwTrue";
    case TokenType::KwFalse: return "KwFalse";
    case TokenType::TyI32: return "TyI32";
    case TokenType::TyI64: return "TyI64";
    case TokenType::TyF64: return "TyF64";
    case TokenType::TyBool: return "TyBool";
    case TokenType::TyVoid: return "TyVoid";
    case TokenType::TyString: return "TyString";
    case TokenType::Bang: return "Bang";
    case TokenType::Equal: return "Equal";
    case TokenType::EqEq: return "EqEq";
    case TokenType::NotEq: return "NotEq";
    case TokenType::ColonAssign: return "ColonAssign";
    case TokenType::Arrow: return "Arrow";
    case TokenType::Colon: return "Colon";
    case TokenType::Semicolon: return "Semicolon";
    case TokenType::Comma: return "Comma";
    case TokenType::Dot: return "Dot";
    case TokenType::Plus: return "Plus";
    case TokenType::Minus: return "Minus";
    case TokenType::Star: return "Star";
    case TokenType::Slash: return "Slash";
    case TokenType::AmpAmp: return "AmpAmp";
    case TokenType::Pipe: return "Pipe";
    case TokenType::PipePipe: return "PipePipe";
    case TokenType::LParen: return "LParen";
    case TokenType::RParen: return "RParen";
    case TokenType::LBrace: return "LBrace";
    case TokenType::RBrace: return "RBrace";
    case TokenType::LAngle: return "LAngle";
    case TokenType::RAngle: return "RAngle";
    case TokenType::LessEq: return "LessEq";
    case TokenType::GreaterEq: return "GreaterEq";
  }
  return "Unknown";
}

void IntLiteralExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "IntLiteralExpr " << value << '\n';
}

void BooleanLiteralExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "BooleanLiteralExpr " << (value ? "true" : "false") << '\n';
}

void IdentifierExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "IdentifierExpr " << name << '\n';
}

void BinaryExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "BinaryExpr " << op << '\n';
  if (left) left->dump(out, indent + 1);
  if (right) right->dump(out, indent + 1);
}

void CallExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "CallExpr " << callee << " [" << args.size() << "]\n";
  for (const ExprPtr& arg : args) {
    if (arg) arg->dump(out, indent + 1);
  }
}

void VarDeclStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "VarDeclStmt ";
  switch (decl_kind) {
    case VarDeclKind::Constd: out << "constd " << name; break;
    case VarDeclKind::ExplicitType: out << type_name << ' ' << name; break;
    case VarDeclKind::Infer: out << ":= " << name; break;
  }
  out << '\n';
  if (init) init->dump(out, indent + 1);
}

void AssignStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "AssignStmt " << name << '\n';
  if (value) value->dump(out, indent + 1);
}

void Param::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "Param " << type_name << ' ' << name << '\n';
}

void ReturnStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ReturnStmt\n";
  if (value) value->dump(out, indent + 1);
}

void FuncDeclStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "FuncDeclStmt " << name << " -> " << return_type << '\n';
  for (const Param& p : params) p.dump(out, indent + 1);
  dumpStmts(out, indent + 1, body);
}

void ExprStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ExprStmt\n";
  if (expr) expr->dump(out, indent + 1);
}

void IfStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "IfStmt\n";
  if (cond) cond->dump(out, indent + 1);
  pad(out, indent + 1);
  out << "Then\n";
  dumpStmts(out, indent + 2, then_body);
  if (!else_body.empty()) {
    pad(out, indent + 1);
    out << "Else\n";
    dumpStmts(out, indent + 2, else_body);
  }
}

void WhileStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "WhileStmt\n";
  if (cond) cond->dump(out, indent + 1);
  dumpStmts(out, indent + 1, body);
}

void Module::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "Module\n";
  dumpStmts(out, indent + 1, stmts);
}

}  // namespace l::ast
