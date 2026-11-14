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
    case TokenType::FloatLiteral: return "FloatLiteral";
    case TokenType::StringLiteral: return "StringLiteral";
    case TokenType::LcmdDirective: return "LcmdDirective";
    case TokenType::KwConst: return "KwConst";
    case TokenType::KwConstd: return "KwConstd";
    case TokenType::KwFun: return "KwFun";
    case TokenType::KwReturn: return "KwReturn";
    case TokenType::KwInterface: return "KwInterface";
    case TokenType::KwPartial: return "KwPartial";
    case TokenType::KwClass: return "KwClass";
    case TokenType::KwIf: return "KwIf";
    case TokenType::KwElse: return "KwElse";
    case TokenType::KwMatch: return "KwMatch";
    case TokenType::KwAs: return "KwAs";
    case TokenType::KwWhile: return "KwWhile";
    case TokenType::KwFor: return "KwFor";
    case TokenType::KwIn: return "KwIn";
    case TokenType::KwTrue: return "KwTrue";
    case TokenType::KwFalse: return "KwFalse";
    case TokenType::KwHeap: return "KwHeap";
    case TokenType::KwArena: return "KwArena";
    case TokenType::KwDefer: return "KwDefer";
    case TokenType::KwSync: return "KwSync";
    case TokenType::KwSpawn: return "KwSpawn";
    case TokenType::KwComptime: return "KwComptime";
    case TokenType::KwImport: return "KwImport";
    case TokenType::KwPub: return "KwPub";
    case TokenType::KwStatic: return "KwStatic";
    case TokenType::KwExtern: return "KwExtern";
    case TokenType::KwModule: return "KwModule";
    case TokenType::KwDelegate: return "KwDelegate";
    case TokenType::TyI32: return "TyI32";
    case TokenType::TyI64: return "TyI64";
    case TokenType::TyF64: return "TyF64";
    case TokenType::TyBool: return "TyBool";
    case TokenType::TyVoid: return "TyVoid";
    case TokenType::TyString: return "TyString";
    case TokenType::TyPtr: return "TyPtr";
    case TokenType::Bang: return "Bang";
    case TokenType::Question: return "Question";
    case TokenType::QuestionQuestion: return "QuestionQuestion";
    case TokenType::Equal: return "Equal";
    case TokenType::EqEq: return "EqEq";
    case TokenType::NotEq: return "NotEq";
    case TokenType::ColonAssign: return "ColonAssign";
    case TokenType::Arrow: return "Arrow";
    case TokenType::FatArrow: return "FatArrow";
    case TokenType::Colon: return "Colon";
    case TokenType::Semicolon: return "Semicolon";
    case TokenType::Comma: return "Comma";
    case TokenType::Dot: return "Dot";
    case TokenType::DotDot: return "DotDot";
    case TokenType::Plus: return "Plus";
    case TokenType::Minus: return "Minus";
    case TokenType::Star: return "Star";
    case TokenType::Slash: return "Slash";
    case TokenType::PlusEqual: return "PlusEqual";
    case TokenType::MinusEqual: return "MinusEqual";
    case TokenType::StarEqual: return "StarEqual";
    case TokenType::SlashEqual: return "SlashEqual";
    case TokenType::PlusPlus: return "PlusPlus";
    case TokenType::MinusMinus: return "MinusMinus";
    case TokenType::AmpAmp: return "AmpAmp";
    case TokenType::Amp: return "Amp";
    case TokenType::Pipe: return "Pipe";
    case TokenType::PipePipe: return "PipePipe";
    case TokenType::LParen: return "LParen";
    case TokenType::RParen: return "RParen";
    case TokenType::LBrace: return "LBrace";
    case TokenType::RBrace: return "RBrace";
    case TokenType::LBracket: return "LBracket";
    case TokenType::RBracket: return "RBracket";
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

void FloatLiteralExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "FloatLiteralExpr " << value << '\n';
}

void BooleanLiteralExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "BooleanLiteralExpr " << (value ? "true" : "false") << '\n';
}

void StringLiteralExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "StringLiteralExpr \"" << value << "\"\n";
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

void CoalesceExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "CoalesceExpr ??\n";
  if (left) left->dump(out, indent + 1);
  if (right) right->dump(out, indent + 1);
}

void CallArg::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  if (name.empty()) {
    out << "CallArg (positional)\n";
  } else {
    out << "CallArg " << name << ":\n";
  }
  if (value) value->dump(out, indent + 1);
}

void CallExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "CallExpr " << callee << " [" << args.size() << "]\n";
  for (const CallArg& arg : args) {
    arg.dump(out, indent + 1);
  }
}

void MemberAccessExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "MemberAccessExpr ." << member << '\n';
  if (object) object->dump(out, indent + 1);
}

void MethodCallExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "MethodCallExpr ." << method << " [" << args.size() << "]\n";
  if (object) object->dump(out, indent + 1);
  for (const CallArg& arg : args) {
    arg.dump(out, indent + 1);
  }
}

void NewExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "NewExpr " << class_name << " [" << args.size() << "]\n";
  for (const CallArg& arg : args) {
    arg.dump(out, indent + 1);
  }
}

void HeapExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "HeapExpr\n";
  if (inner) inner->dump(out, indent + 1);
}

void IndexExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "IndexExpr\n";
  if (object) object->dump(out, indent + 1);
  if (index) index->dump(out, indent + 1);
}

void SizeofExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "SizeofExpr " << type_name << '\n';
}

void ArrayLiteralExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ArrayLiteralExpr [" << elements.size() << "]\n";
  for (const auto& e : elements) {
    if (e) e->dump(out, indent + 1);
  }
}

void LambdaExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "LambdaExpr |";
  for (size_t i = 0; i < params.size(); ++i) {
    if (i) out << ", ";
    out << params[i];
  }
  out << "|";
  if (!captures.empty()) {
    out << " capture[";
    for (size_t i = 0; i < captures.size(); ++i) {
      if (i) out << ", ";
      out << captures[i];
    }
    out << "]";
  }
  out << '\n';
  dumpStmts(out, indent + 1, body);
}

void CastExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "CastExpr as " << type_name << '\n';
  if (value) {
    value->dump(out, indent + 1);
  }
}

void IncDecExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "IncDecExpr " << (is_prefix ? "prefix " : "postfix ") << (is_inc ? "++" : "--")
      << '\n';
  if (target) {
    target->dump(out, indent + 1);
  }
}

void TryExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "TryExpr ?\n";
  if (inner) {
    inner->dump(out, indent + 1);
  }
}

void VarDeclStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "VarDeclStmt ";
  switch (decl_kind) {
    case VarDeclKind::Const: out << "const " << name; break;
    case VarDeclKind::Constd: out << "constd " << name; break;
    case VarDeclKind::ExplicitType: out << type_name << ' ' << name; break;
    case VarDeclKind::Infer: out << ":= " << name; break;
  }
  if (is_mutable) out << " !";
  out << '\n';
  if (init) init->dump(out, indent + 1);
}

void DestructureDeclStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "DestructureDeclStmt (";
  for (std::size_t i = 0; i < bindings.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << bindings[i].name;
    if (bindings[i].is_mutable) {
      out << '!';
    }
  }
  out << ") :=\n";
  if (init) {
    init->dump(out, indent + 1);
  }
}

void AssignStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "AssignStmt" << (mutable_marker ? " !" : "") << '\n';
  if (target) target->dump(out, indent + 1);
  if (value) value->dump(out, indent + 1);
}

void Param::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "Param " << type_name << ' ' << name;
  if (is_mutable) {
    out << " !";
  }
  out << '\n';
}

void ReturnStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ReturnStmt\n";
  if (value) value->dump(out, indent + 1);
}

void FuncDeclStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "FuncDeclStmt " << (is_extern ? "extern " : "") << (is_static ? "static " : "")
      << (is_pub ? "pub " : "") << (is_compiler_generated ? "auto " : "") << name;
  if (!generics.empty()) {
    out << '<';
    for (size_t i = 0; i < generics.size(); ++i) {
      if (i) out << ", ";
      out << generics[i].name << ": " << generics[i].constraint;
    }
    out << '>';
  }
  out << " -> " << return_type << '\n';
  for (const Param& p : params) p.dump(out, indent + 1);
  dumpStmts(out, indent + 1, body);
}

void ExprStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ExprStmt\n";
  if (expr) expr->dump(out, indent + 1);
}

void BlockStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "BlockStmt\n";
  dumpStmts(out, indent + 1, stmts);
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

void MatchStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "MatchStmt\n";
  if (scrutinee) {
    scrutinee->dump(out, indent + 1);
  }
  for (const MatchBranch& br : branches) {
    pad(out, indent + 1);
    if (br.is_wildcard) {
      out << "Branch _\n";
    } else {
      out << "Branch\n";
      if (br.pattern) {
        br.pattern->dump(out, indent + 2);
      }
    }
    dumpStmts(out, indent + 2, br.body);
  }
}

void WhileStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "WhileStmt\n";
  if (cond) cond->dump(out, indent + 1);
  dumpStmts(out, indent + 1, body);
}

void ForInStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ForInStmt " << item_name << '\n';
  if (iterable) iterable->dump(out, indent + 1);
  dumpStmts(out, indent + 1, body);
}

void FieldDeclStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  if (is_delegate) {
    out << "FieldDeclStmt delegate " << type_name << '\n';
    return;
  }
  out << "FieldDeclStmt " << (is_pub ? "pub " : "") << type_name << ' ' << name << '\n';
}

void ClassDeclStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ClassDeclStmt " << (is_pub ? "pub " : "") << (is_partial ? "partial " : "") << name;
  if (!generics.empty()) {
    out << '<';
    for (size_t i = 0; i < generics.size(); ++i) {
      if (i != 0) {
        out << ", ";
      }
      out << generics[i].name;
      if (!generics[i].constraint.empty()) {
        out << ": " << generics[i].constraint;
      }
    }
    out << '>';
  }
  if (!interfaces.empty()) {
    out << " :";
    for (const std::string& iface : interfaces) {
      out << ' ' << iface;
    }
  }
  out << '\n';
  for (const auto& field : fields) {
    if (field) field->dump(out, indent + 1);
  }
  for (const auto& method : methods) {
    if (method) method->dump(out, indent + 1);
  }
}

void InterfaceDeclStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "InterfaceDeclStmt " << (is_pub ? "pub " : "") << name << '\n';
  for (const auto& method : methods) {
    if (method) method->dump(out, indent + 1);
  }
}

void ImportStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ImportStmt \"" << path << "\"\n";
}

void ExternModuleStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ExternModuleStmt " << name << '\n';
  for (const auto& decl : decls) {
    if (decl) {
      decl->dump(out, indent + 1);
    }
  }
}

void DeferStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "DeferStmt\n";
  dumpStmts(out, indent + 1, body);
}

void SyncBlockStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "SyncBlockStmt\n";
  dumpStmts(out, indent + 1, body);
}

void SpawnBlockStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "SpawnBlockStmt\n";
  dumpStmts(out, indent + 1, body);
}

void ComptimeStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "ComptimeStmt\n";
  dumpStmts(out, indent + 1, body);
}

void SpawnExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "SpawnExpr\n";
  dumpStmts(out, indent + 1, body);
}

void AddrOfExpr::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "AddrOfExpr &\n";
  if (operand) {
    operand->dump(out, indent + 1);
  }
}

void DirectiveStmt::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "DirectiveStmt";
  if (!context_path.empty()) {
    out << " @" << context_path;
  }
  out << ' ' << command << "(" << args << ")\n";
}

void Module::dump(std::ostream& out, int indent) const {
  pad(out, indent);
  out << "Module\n";
  for (const auto& h : init_hooks) {
    if (h) {
      pad(out, indent + 1);
      out << "LifecycleHook __init\n";
      h->dump(out, indent + 2);
    }
  }
  for (const auto& h : deinit_hooks) {
    if (h) {
      pad(out, indent + 1);
      out << "LifecycleHook __deinit\n";
      h->dump(out, indent + 2);
    }
  }
  dumpStmts(out, indent + 1, stmts);
}

}  // namespace l::ast
