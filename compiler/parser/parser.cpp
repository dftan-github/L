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
  if (check(ast::TokenType::KwReturn)) {
    return parseReturnStmt();
  }
  if (check(ast::TokenType::KwIf)) {
    return parseIfStmt();
  }
  if (check(ast::TokenType::KwWhile)) {
    return parseWhileStmt();
  }
  if (check(ast::TokenType::KwConstd)) {
    return parseConstdDecl();
  }
  if (isTypeToken(peek().type)) {
    return parseExplicitTypeDecl();
  }
  if (check(ast::TokenType::Identifier)) {
    if (peek(1).type == ast::TokenType::ColonAssign) {
      return parseInferDecl();
    }
    // name = expr;  or  name! = expr;
    if (peek(1).type == ast::TokenType::Equal ||
        (peek(1).type == ast::TokenType::Bang && peek(2).type == ast::TokenType::Equal)) {
      return parseAssignStmt();
    }
  }
  if (check(ast::TokenType::Identifier) || check(ast::TokenType::IntLiteral) ||
      check(ast::TokenType::LParen) || check(ast::TokenType::KwTrue) ||
      check(ast::TokenType::KwFalse)) {
    return parseExprStmt();
  }
  return nullptr;
}

ast::ExprPtr Parser::parseExpression(int min_precedence) {
  ast::ExprPtr left = parsePrimaryExpression();
  if (!left) {
    return nullptr;
  }

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
  if (check(ast::TokenType::IntLiteral)) {
    ast::Token tok = peek();
    advance();
    auto expr = std::make_unique<ast::IntLiteralExpr>();
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
    return inner;
  }
  if (check(ast::TokenType::Identifier)) {
    ast::Token name = peek();
    advance();
    if (check(ast::TokenType::LParen)) {
      return parseCallArgs(name.lexeme, name.loc);
    }
    auto expr = std::make_unique<ast::IdentifierExpr>();
    expr->loc = name.loc;
    expr->name = name.lexeme;
    return expr;
  }
  errorAt(peek(), "expected expression");
  return nullptr;
}

ast::ExprPtr Parser::parseCallArgs(std::string callee, ast::SourceLocation loc) {
  consume(ast::TokenType::LParen, "expected '(' after function name");
  auto call = std::make_unique<ast::CallExpr>();
  call->loc = std::move(loc);
  call->callee = std::move(callee);

  if (!check(ast::TokenType::RParen)) {
    for (;;) {
      ast::ExprPtr arg = parseExpression(0);
      if (!arg) {
        return nullptr;
      }
      call->args.push_back(std::move(arg));
      if (!match(ast::TokenType::Comma)) {
        break;
      }
    }
  }
  consume(ast::TokenType::RParen, "expected ')' after argument list");
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
  ast::Token ty = peek();
  if (!isTypeToken(ty.type)) {
    errorAt(ty, "expected type");
    return nullptr;
  }
  advance();
  ast::Token name = consume(ast::TokenType::Identifier, "expected name after type");
  consume(ast::TokenType::Equal, "expected '=' after typed name");
  ast::ExprPtr init = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after typed declaration");

  auto stmt = std::make_unique<ast::VarDeclStmt>();
  stmt->loc = ty.loc;
  stmt->decl_kind = ast::VarDeclKind::ExplicitType;
  stmt->type_name = typeTokenLexeme(ty);
  stmt->name = name.lexeme;
  stmt->init = std::move(init);
  return stmt;
}

ast::StmtPtr Parser::parseInferDecl() {
  ast::Token name = consume(ast::TokenType::Identifier, "expected name before ':='");
  consume(ast::TokenType::ColonAssign, "expected ':='");
  ast::ExprPtr init = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after ':=' declaration");

  auto stmt = std::make_unique<ast::VarDeclStmt>();
  stmt->loc = name.loc;
  stmt->decl_kind = ast::VarDeclKind::Infer;
  stmt->name = name.lexeme;
  stmt->init = std::move(init);
  return stmt;
}

ast::StmtPtr Parser::parseAssignStmt() {
  ast::Token name = consume(ast::TokenType::Identifier, "expected name in assignment");
  match(ast::TokenType::Bang);  // optional mutability marker on LHS
  consume(ast::TokenType::Equal, "expected '=' in assignment");
  ast::ExprPtr value = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after assignment");

  auto stmt = std::make_unique<ast::AssignStmt>();
  stmt->loc = name.loc;
  stmt->name = name.lexeme;
  stmt->value = std::move(value);
  return stmt;
}

ast::Param Parser::parseParam() {
  ast::Param param;
  if (!isTypeToken(peek().type)) {
    errorAt(peek(), "expected parameter type");
    return param;
  }
  ast::Token ty = peek();
  advance();
  ast::Token name = consume(ast::TokenType::Identifier, "expected parameter name");
  param.type_name = typeTokenLexeme(ty);
  param.name = name.lexeme;
  param.loc = ty.loc;
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

ast::StmtPtr Parser::parseFuncDecl() {
  ast::Token kw = consume(ast::TokenType::KwFun, "expected 'fun'");
  ast::Token name = consume(ast::TokenType::Identifier, "expected function name");
  consume(ast::TokenType::LParen, "expected '(' after function name");

  auto func = std::make_unique<ast::FuncDeclStmt>();
  func->loc = kw.loc;
  func->name = name.lexeme;

  if (!check(ast::TokenType::RParen)) {
    for (;;) {
      func->params.push_back(parseParam());
      if (!match(ast::TokenType::Comma)) {
        break;
      }
    }
  }
  consume(ast::TokenType::RParen, "expected ')' after parameter list");
  consume(ast::TokenType::Arrow, "expected '->' after parameter list");

  if (!isTypeToken(peek().type)) {
    errorAt(peek(), "expected return type after '->'");
    return func;
  }
  func->return_type = typeTokenLexeme(peek());
  advance();
  func->body = parseBlockBody();
  return func;
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

bool Parser::isTypeToken(ast::TokenType type) {
  switch (type) {
    case ast::TokenType::TyI32:
    case ast::TokenType::TyI64:
    case ast::TokenType::TyF64:
    case ast::TokenType::TyBool:
    case ast::TokenType::TyVoid:
    case ast::TokenType::TyString:
      return true;
    default:
      return false;
  }
}

std::string Parser::typeTokenLexeme(const ast::Token& tok) {
  if (!tok.lexeme.empty()) {
    return tok.lexeme;
  }
  return ast::toString(tok.type);
}

}  // namespace l::parser
