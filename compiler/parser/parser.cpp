#include "parser/parser.hpp"

#include <sstream>
#include <utility>

namespace l::parser {

Parser::Parser(std::vector<ast::Token> tokens) : tokens_(std::move(tokens)) {
  if (tokens_.empty()) {
    ast::Token eof;
    eof.type = ast::TokenType::EndOfFile;
    tokens_.push_back(std::move(eof));
  }
}

void Parser::registerClassName(const std::string& name) {
  class_names_.insert(name);
}

void Parser::registerInterfaceName(const std::string& name) {
  interface_names_.insert(name);
}

std::unique_ptr<ast::Module> Parser::parseModule() {
  auto module = std::make_unique<ast::Module>();
  while (!atEnd()) {
    ast::StmtPtr stmt = parseStatement();
    if (stmt) {
      module->stmts.push_back(std::move(stmt));
    } else if (had_error_) {
      synchronize();
      if (atEnd()) {
        break;
      }
    } else {
      errorAt(peek(), "expected statement");
      synchronize();
    }
  }
  return module;
}

ast::StmtPtr Parser::parseStatement() {
  if (check(ast::TokenType::KwFun)) {
    return parseFuncDecl();
  }
  if (check(ast::TokenType::KwInterface)) {
    return parseInterfaceDecl();
  }
  if (check(ast::TokenType::KwClass)) {
    return parseClassDecl();
  }
  if (check(ast::TokenType::KwReturn)) {
    return parseReturnStmt();
  }
  if (check(ast::TokenType::KwIf)) {
    return parseIfStmt();
  }
  if (check(ast::TokenType::KwWhile)) {
    return parseWhileStmt();
  }
  if (check(ast::TokenType::KwDefer)) {
    return parseDeferStmt();
  }
  if (check(ast::TokenType::KwSync)) {
    return parseSyncBlock();
  }
  if (check(ast::TokenType::KwConstd)) {
    return parseConstdDecl();
  }
  if (looksLikeTypedDecl()) {
    return parseExplicitTypeDecl();
  }
  if (check(ast::TokenType::Identifier) && peek(1).type == ast::TokenType::Bang &&
      peek(2).type == ast::TokenType::ColonAssign) {
    return parseInferDecl();
  }
  if (check(ast::TokenType::Identifier) && peek(1).type == ast::TokenType::ColonAssign) {
    return parseInferDecl();
  }
  if (check(ast::TokenType::Identifier) || check(ast::TokenType::IntLiteral) ||
      check(ast::TokenType::StringLiteral) || check(ast::TokenType::TyString) ||
      check(ast::TokenType::LParen) || check(ast::TokenType::KwTrue) ||
      check(ast::TokenType::KwFalse) || check(ast::TokenType::KwHeap)) {
    return parseAssignOrExprStmt();
  }
  return nullptr;
}

ast::ExprPtr Parser::parseExpression(int min_precedence) {
  ast::ExprPtr left = parsePrimaryExpression();
  if (!left) {
    return nullptr;
  }
  left = parsePostfix(std::move(left));

  for (;;) {
    const int prec = infixPrecedence(peek().type);
    if (prec < min_precedence) {
      break;
    }

    ast::Token op_tok = peek();
    advance();
    ast::ExprPtr right = parseExpression(prec + 1);
    if (!right) {
      return nullptr;
    }

    auto binary = std::make_unique<ast::BinaryExpr>();
    binary->loc = op_tok.loc;
    binary->op = op_tok.lexeme;
    binary->left = std::move(left);
    binary->right = std::move(right);
    left = std::move(binary);
  }

  return left;
}

ast::ExprPtr Parser::parsePostfix(ast::ExprPtr left) {
  while (left) {
    if (match(ast::TokenType::Dot)) {
      ast::Token field = consume(ast::TokenType::Identifier, "expected name after '.'");
      if (check(ast::TokenType::LParen)) {
        auto call = std::make_unique<ast::MethodCallExpr>();
        call->loc = field.loc;
        call->object = std::move(left);
        call->method = field.lexeme;
        call->args = parseArgList();
        left = std::move(call);
        continue;
      }
      auto access = std::make_unique<ast::MemberAccessExpr>();
      access->loc = field.loc;
      access->object = std::move(left);
      access->member = field.lexeme;
      left = std::move(access);
      continue;
    }
    break;
  }
  return left;
}

std::vector<ast::ExprPtr> Parser::parseArgList() {
  std::vector<ast::ExprPtr> args;
  consume(ast::TokenType::LParen, "expected '('");
  if (!check(ast::TokenType::RParen)) {
    for (;;) {
      ast::ExprPtr arg = parseExpression(0);
      if (!arg) {
        break;
      }
      args.push_back(std::move(arg));
      if (!match(ast::TokenType::Comma)) {
        break;
      }
    }
  }
  consume(ast::TokenType::RParen, "expected ')' after argument list");
  return args;
}

int Parser::infixPrecedence(ast::TokenType type) {
  switch (type) {
    case ast::TokenType::PipePipe:
      return 1;
    case ast::TokenType::AmpAmp:
      return 2;
    case ast::TokenType::EqEq:
    case ast::TokenType::NotEq:
      return 3;
    case ast::TokenType::LAngle:
    case ast::TokenType::RAngle:
    case ast::TokenType::LessEq:
    case ast::TokenType::GreaterEq:
      return 4;
    case ast::TokenType::Plus:
    case ast::TokenType::Minus:
      return 10;
    case ast::TokenType::Star:
    case ast::TokenType::Slash:
      return 20;
    default:
      return -1;
  }
}

ast::ExprPtr Parser::parsePrimaryExpression() {
  if (check(ast::TokenType::KwHeap)) {
    ast::Token kw = peek();
    advance();
    ast::ExprPtr inner = parsePrimaryExpression();
    if (!inner) {
      return nullptr;
    }
    auto expr = std::make_unique<ast::HeapExpr>();
    expr->loc = kw.loc;
    expr->inner = std::move(inner);
    return expr;
  }
  if (check(ast::TokenType::IntLiteral)) {
    ast::Token tok = peek();
    advance();
    auto expr = std::make_unique<ast::IntLiteralExpr>();
    expr->loc = tok.loc;
    expr->value = tok.lexeme;
    return expr;
  }
  if (check(ast::TokenType::StringLiteral)) {
    ast::Token tok = peek();
    advance();
    auto expr = std::make_unique<ast::StringLiteralExpr>();
    expr->loc = tok.loc;
    expr->value = tok.lexeme;
    return expr;
  }
  if (check(ast::TokenType::KwTrue) || check(ast::TokenType::KwFalse)) {
    ast::Token tok = peek();
    advance();
    auto expr = std::make_unique<ast::BooleanLiteralExpr>();
    expr->loc = tok.loc;
    expr->value = (tok.type == ast::TokenType::KwTrue);
    return expr;
  }
  if (check(ast::TokenType::LParen)) {
    advance();
    ast::ExprPtr inner = parseExpression(0);
    consume(ast::TokenType::RParen, "expected ')' after expression");
    return parsePostfix(std::move(inner));
  }
  // Identifiers and type-keyword names used as constructors (e.g. String(...)).
  if (check(ast::TokenType::Identifier) || check(ast::TokenType::TyString)) {
    ast::Token name = peek();
    advance();
    const std::string name_text =
        name.type == ast::TokenType::TyString ? "String" : name.lexeme;
    if (check(ast::TokenType::LParen)) {
      if (class_names_.count(name_text) != 0) {
        auto expr = std::make_unique<ast::NewExpr>();
        expr->loc = name.loc;
        expr->class_name = name_text;
        expr->args = parseArgList();
        return expr;
      }
      return parseCallArgs(name_text, name.loc);
    }
    auto expr = std::make_unique<ast::IdentifierExpr>();
    expr->loc = name.loc;
    expr->name = name_text;
    return expr;
  }
  errorAt(peek(), "expected expression");
  return nullptr;
}

ast::ExprPtr Parser::parseCallArgs(std::string callee, ast::SourceLocation loc) {
  auto call = std::make_unique<ast::CallExpr>();
  call->loc = std::move(loc);
  call->callee = std::move(callee);
  call->args = parseArgList();
  return call;
}

ast::StmtPtr Parser::parseConstdDecl() {
  ast::Token kw = consume(ast::TokenType::KwConstd, "expected 'constd'");
  ast::Token name = consume(ast::TokenType::Identifier, "expected name after constd");
  consume(ast::TokenType::Equal, "expected '=' after constd name");
  ast::ExprPtr init = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after constd declaration");

  auto stmt = std::make_unique<ast::VarDeclStmt>();
  stmt->loc = kw.loc;
  stmt->decl_kind = ast::VarDeclKind::Constd;
  stmt->name = name.lexeme;
  stmt->init = std::move(init);
  return stmt;
}

ast::StmtPtr Parser::parseExplicitTypeDecl() {
  ast::SourceLocation loc = peek().loc;
  std::string type_name = parseTypeName();
  if (type_name.empty()) {
    errorAt(peek(), "expected type");
    return nullptr;
  }
  ast::Token name = consume(ast::TokenType::Identifier, "expected name after type");
  const bool is_mutable = match(ast::TokenType::Bang);
  consume(ast::TokenType::Equal, "expected '=' after typed name");
  ast::ExprPtr init = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after typed declaration");

  auto stmt = std::make_unique<ast::VarDeclStmt>();
  stmt->loc = loc;
  stmt->decl_kind = ast::VarDeclKind::ExplicitType;
  stmt->type_name = std::move(type_name);
  stmt->name = name.lexeme;
  stmt->is_mutable = is_mutable;
  stmt->init = std::move(init);
  return stmt;
}

ast::StmtPtr Parser::parseInferDecl() {
  ast::Token name = consume(ast::TokenType::Identifier, "expected name before ':='");
  const bool is_mutable = match(ast::TokenType::Bang);
  consume(ast::TokenType::ColonAssign, "expected ':='");
  ast::ExprPtr init = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after ':=' declaration");

  auto stmt = std::make_unique<ast::VarDeclStmt>();
  stmt->loc = name.loc;
  stmt->decl_kind = ast::VarDeclKind::Infer;
  stmt->name = name.lexeme;
  stmt->is_mutable = is_mutable;
  stmt->init = std::move(init);
  return stmt;
}

ast::StmtPtr Parser::parseAssignOrExprStmt() {
  ast::Token start = peek();
  ast::ExprPtr expr = parseExpression(0);
  if (!expr) {
    return nullptr;
  }

  const bool mutable_marker = match(ast::TokenType::Bang);
  if (match(ast::TokenType::Equal)) {
    ast::ExprPtr value = parseExpression(0);
    consume(ast::TokenType::Semicolon, "expected ';' after assignment");
    auto stmt = std::make_unique<ast::AssignStmt>();
    stmt->loc = start.loc;
    stmt->target = std::move(expr);
    stmt->value = std::move(value);
    stmt->mutable_marker = mutable_marker;
    return stmt;
  }
  if (mutable_marker) {
    errorAt(peek(), "'!' is only valid on mutable assignment LHS");
  }

  consume(ast::TokenType::Semicolon, "expected ';' after expression");
  auto stmt = std::make_unique<ast::ExprStmt>();
  stmt->loc = start.loc;
  stmt->expr = std::move(expr);
  return stmt;
}

ast::Param Parser::parseParam() {
  ast::Param param;
  if (!isTypeStart()) {
    errorAt(peek(), "expected parameter type");
    return param;
  }
  param.loc = peek().loc;
  param.type_name = parseTypeName();
  ast::Token name = consume(ast::TokenType::Identifier, "expected parameter name");
  param.name = name.lexeme;
  return param;
}

std::vector<ast::StmtPtr> Parser::parseBlockBody() {
  std::vector<ast::StmtPtr> body;
  consume(ast::TokenType::LBrace, "expected '{'");
  while (!atEnd() && !check(ast::TokenType::RBrace)) {
    ast::StmtPtr stmt = parseStatement();
    if (stmt) {
      body.push_back(std::move(stmt));
      continue;
    }
    if (had_error_) {
      synchronize();
      continue;
    }
    errorAt(peek(), "expected statement in block");
    synchronize();
  }
  consume(ast::TokenType::RBrace, "expected '}'");
  return body;
}

bool Parser::parseGenericParams(std::vector<ast::GenericParam>& out) {
  // <T: IReader, U: IFoo>
  consume(ast::TokenType::LAngle, "expected '<' to start generic parameters");
  for (;;) {
    ast::Token tname = consume(ast::TokenType::Identifier, "expected generic type name");
    consume(ast::TokenType::Colon, "expected ':' after generic name (naked <T> is forbidden)");
    ast::Token constraint =
        consume(ast::TokenType::Identifier, "expected interface constraint after ':'");
    ast::GenericParam gp;
    gp.loc = tname.loc;
    gp.name = tname.lexeme;
    gp.constraint = constraint.lexeme;
    if (generic_type_names_.count(gp.name) != 0) {
      errorAt(tname, "duplicate generic type parameter");
    }
    generic_type_names_.insert(gp.name);
    out.push_back(std::move(gp));
    if (!match(ast::TokenType::Comma)) {
      break;
    }
  }
  consume(ast::TokenType::RAngle, "expected '>' after generic parameters");
  return true;
}

std::unique_ptr<ast::FuncDeclStmt> Parser::parseFunCommon(bool allow_body) {
  ast::Token kw = consume(ast::TokenType::KwFun, "expected 'fun'");
  ast::Token name = consume(ast::TokenType::Identifier, "expected function name");

  auto func = std::make_unique<ast::FuncDeclStmt>();
  func->loc = kw.loc;
  func->name = name.lexeme;

  const auto saved_generics = generic_type_names_;
  if (check(ast::TokenType::LAngle)) {
    parseGenericParams(func->generics);
  }

  consume(ast::TokenType::LParen, "expected '(' after function name");

  if (!check(ast::TokenType::RParen)) {
    for (;;) {
      func->params.push_back(parseParam());
      if (!match(ast::TokenType::Comma)) {
        break;
      }
    }
  }
  consume(ast::TokenType::RParen, "expected ')' after parameter list");

  if (match(ast::TokenType::Arrow)) {
    if (!isTypeStart()) {
      errorAt(peek(), "expected return type after '->'");
      generic_type_names_ = saved_generics;
      return func;
    }
    func->return_type = parseTypeName();
  } else {
    func->return_type = "void";
  }

  if (allow_body && check(ast::TokenType::LBrace)) {
    func->body = parseBlockBody();
  } else {
    consume(ast::TokenType::Semicolon, "expected ';' after method signature");
  }

  generic_type_names_ = saved_generics;
  return func;
}

ast::StmtPtr Parser::parseFuncDecl() {
  return parseFunCommon(/*allow_body=*/true);
}

ast::StmtPtr Parser::parseReturnStmt() {
  ast::Token kw = consume(ast::TokenType::KwReturn, "expected 'return'");
  ast::ExprPtr value = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after return");

  auto stmt = std::make_unique<ast::ReturnStmt>();
  stmt->loc = kw.loc;
  stmt->value = std::move(value);
  return stmt;
}

ast::StmtPtr Parser::parseExprStmt() {
  ast::Token start = peek();
  ast::ExprPtr expr = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after expression");
  auto stmt = std::make_unique<ast::ExprStmt>();
  stmt->loc = start.loc;
  stmt->expr = std::move(expr);
  return stmt;
}

ast::StmtPtr Parser::parseIfStmt() {
  ast::Token kw = consume(ast::TokenType::KwIf, "expected 'if'");
  ast::ExprPtr cond = parseExpression(0);
  auto stmt = std::make_unique<ast::IfStmt>();
  stmt->loc = kw.loc;
  stmt->cond = std::move(cond);
  stmt->then_body = parseBlockBody();
  if (match(ast::TokenType::KwElse)) {
    stmt->else_body = parseBlockBody();
  }
  return stmt;
}

ast::StmtPtr Parser::parseWhileStmt() {
  ast::Token kw = consume(ast::TokenType::KwWhile, "expected 'while'");
  ast::ExprPtr cond = parseExpression(0);
  auto stmt = std::make_unique<ast::WhileStmt>();
  stmt->loc = kw.loc;
  stmt->cond = std::move(cond);
  stmt->body = parseBlockBody();
  return stmt;
}

ast::StmtPtr Parser::parseDeferStmt() {
  ast::Token kw = consume(ast::TokenType::KwDefer, "expected 'defer'");
  auto stmt = std::make_unique<ast::DeferStmt>();
  stmt->loc = kw.loc;
  if (check(ast::TokenType::LBrace)) {
    stmt->body = parseBlockBody();
  } else {
    ast::StmtPtr inner = parseStatement();
    if (!inner) {
      errorAt(peek(), "expected statement after defer");
      return stmt;
    }
    stmt->body.push_back(std::move(inner));
  }
  return stmt;
}

ast::StmtPtr Parser::parseSyncBlock() {
  ast::Token kw = consume(ast::TokenType::KwSync, "expected 'sync'");
  auto stmt = std::make_unique<ast::SyncBlockStmt>();
  stmt->loc = kw.loc;
  stmt->body = parseBlockBody();
  return stmt;
}

ast::StmtPtr Parser::parseInterfaceDecl() {
  ast::Token kw = consume(ast::TokenType::KwInterface, "expected 'interface'");
  ast::Token name = consume(ast::TokenType::Identifier, "expected interface name");
  consume(ast::TokenType::LBrace, "expected '{' after interface name");

  auto iface = std::make_unique<ast::InterfaceDeclStmt>();
  iface->loc = kw.loc;
  iface->name = name.lexeme;
  interface_names_.insert(name.lexeme);

  while (!atEnd() && !check(ast::TokenType::RBrace)) {
    if (!check(ast::TokenType::KwFun)) {
      errorAt(peek(), "expected method signature in interface");
      synchronize();
      continue;
    }
    auto method = parseFunCommon(/*allow_body=*/false);
    method->is_method = true;
    iface->methods.push_back(std::move(method));
  }

  consume(ast::TokenType::RBrace, "expected '}' after interface body");
  return iface;
}

ast::StmtPtr Parser::parseClassDecl() {
  ast::Token kw = consume(ast::TokenType::KwClass, "expected 'class'");
  // `String` lexes as TyString; allow it as a class name (stdlib type).
  std::string class_name;
  if (check(ast::TokenType::Identifier)) {
    class_name = peek().lexeme;
    advance();
  } else if (check(ast::TokenType::TyString)) {
    class_name = "String";
    advance();
  } else {
    errorAt(peek(), "expected class name");
    class_name = "?";
  }

  auto cls = std::make_unique<ast::ClassDeclStmt>();
  cls->loc = kw.loc;
  cls->name = class_name;
  class_names_.insert(class_name);

  if (match(ast::TokenType::Colon)) {
    for (;;) {
      ast::Token iface = consume(ast::TokenType::Identifier, "expected interface name after ':'");
      cls->interfaces.push_back(iface.lexeme);
      if (!match(ast::TokenType::Comma)) {
        break;
      }
    }
  }

  consume(ast::TokenType::LBrace, "expected '{' after class header");

  while (!atEnd() && !check(ast::TokenType::RBrace)) {
    if (check(ast::TokenType::KwFun)) {
      auto method = parseFunCommon(/*allow_body=*/true);
      method->is_method = true;
      cls->methods.push_back(std::move(method));
      continue;
    }
    if (!isTypeStart()) {
      errorAt(peek(), "expected field or method in class body");
      synchronize();
      continue;
    }
    auto field = std::make_unique<ast::FieldDeclStmt>();
    field->loc = peek().loc;
    field->type_name = parseTypeName();
    ast::Token field_name =
        consume(ast::TokenType::Identifier, "expected field name");
    field->name = field_name.lexeme;
    consume(ast::TokenType::Semicolon, "expected ';' after field declaration");
    cls->fields.push_back(std::move(field));
  }

  consume(ast::TokenType::RBrace, "expected '}' after class body");
  return cls;
}

const ast::Token& Parser::peek(std::size_t offset) const {
  const std::size_t i = index_ + offset;
  if (i >= tokens_.size()) {
    return tokens_.back();
  }
  return tokens_[i];
}

bool Parser::check(ast::TokenType type) const {
  return peek().type == type;
}

bool Parser::match(ast::TokenType type) {
  if (!check(type)) {
    return false;
  }
  advance();
  return true;
}

ast::Token Parser::consume(ast::TokenType type, const char* message) {
  if (check(type)) {
    ast::Token tok = peek();
    advance();
    return tok;
  }
  errorAt(peek(), message);
  return peek();
}

void Parser::advance() {
  if (!atEnd()) {
    ++index_;
  }
}

bool Parser::atEnd() const {
  return peek().type == ast::TokenType::EndOfFile;
}

void Parser::errorAt(const ast::Token& tok, const std::string& message) {
  had_error_ = true;
  std::ostringstream ss;
  ss << tok.loc.filename << ':' << tok.loc.line << ':' << tok.loc.column << ": "
     << message << " (found " << ast::toString(tok.type) << ")";
  diagnostics_.push_back(ss.str());
}

void Parser::synchronize() {
  while (!atEnd()) {
    if (peek().type == ast::TokenType::Semicolon) {
      advance();
      return;
    }
    if (peek().type == ast::TokenType::RBrace) {
      return;
    }
    advance();
  }
}

bool Parser::isBuiltinTypeToken(ast::TokenType type) {
  switch (type) {
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

bool Parser::isTypeStart() const {
  if (isBuiltinTypeToken(peek().type)) {
    return true;
  }
  if (peek().type != ast::TokenType::Identifier) {
    return false;
  }
  return class_names_.count(peek().lexeme) != 0 ||
         interface_names_.count(peek().lexeme) != 0 ||
         generic_type_names_.count(peek().lexeme) != 0;
}

bool Parser::looksLikeTypedDecl() const {
  if (isBuiltinTypeToken(peek().type)) {
    return peek(1).type == ast::TokenType::Identifier;
  }
  if (peek().type == ast::TokenType::Identifier &&
      (class_names_.count(peek().lexeme) != 0 ||
       interface_names_.count(peek().lexeme) != 0) &&
      peek(1).type == ast::TokenType::Identifier) {
    const auto t2 = peek(2).type;
    return t2 == ast::TokenType::Equal || t2 == ast::TokenType::Bang;
  }
  return false;
}

std::string Parser::parseTypeName() {
  if (isBuiltinTypeToken(peek().type)) {
    ast::Token tok = peek();
    advance();
    return typeTokenLexeme(tok);
  }
  if (peek().type == ast::TokenType::Identifier) {
    std::string name = peek().lexeme;
    advance();
    return name;
  }
  return {};
}

std::string Parser::typeTokenLexeme(const ast::Token& tok) {
  if (tok.type == ast::TokenType::TyString) {
    return "String";
  }
  if (tok.type == ast::TokenType::TyPtr) {
    return "ptr";
  }
  if (!tok.lexeme.empty()) {
    return tok.lexeme;
  }
  return ast::toString(tok.type);
}

}  // namespace l::parser
