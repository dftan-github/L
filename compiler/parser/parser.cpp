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
  if (check(ast::TokenType::KwImport)) {
    return parseImportStmt();
  }
  if (check(ast::TokenType::KwPub)) {
    return parsePubDecl();
  }
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
  if (check(ast::TokenType::KwFor)) {
    return parseForInStmt();
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
    if (match(ast::TokenType::LBracket)) {
      auto index = std::make_unique<ast::IndexExpr>();
      index->loc = left->loc;
      index->object = std::move(left);
      index->index = parseExpression(0);
      consume(ast::TokenType::RBracket, "expected ']' after index expression");
      left = std::move(index);
      continue;
    }
    break;
  }
  return left;
}

std::vector<ast::CallArg> Parser::parseArgList() {
  // Ruby-style: positional args first, then `name: value` keyword args.
  // Once a KW arg appears, every following arg must be keyword-labeled.
  std::vector<ast::CallArg> args;
  consume(ast::TokenType::LParen, "expected '('");
  bool seen_kw = false;
  if (!check(ast::TokenType::RParen)) {
    for (;;) {
      ast::CallArg arg;
      arg.loc = peek().loc;
      const bool looks_kw =
          check(ast::TokenType::Identifier) && peek(1).type == ast::TokenType::Colon;
      if (looks_kw) {
        arg.name = peek().lexeme;
        advance();  // name
        consume(ast::TokenType::Colon, "expected ':' after keyword argument name");
        arg.value = parseExpression(0);
        seen_kw = true;
      } else {
        if (seen_kw) {
          errorAt(peek(), "positional argument cannot follow keyword argument");
        }
        arg.value = parseExpression(0);
      }
      if (!arg.value && !had_error_) {
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

    // sizeof(T) — compile-time size via DataLayout after monomorphization.
    if (name_text == "sizeof" && check(ast::TokenType::LParen)) {
      advance();
      auto expr = std::make_unique<ast::SizeofExpr>();
      expr->loc = name.loc;
      if (!isTypeStart() && !check(ast::TokenType::Identifier)) {
        errorAt(peek(), "expected type name in sizeof(...)");
        expr->type_name = "i32";
      } else {
        expr->type_name = parseTypeName();
      }
      consume(ast::TokenType::RParen, "expected ')' after sizeof type");
      return expr;
    }

    std::string type_or_name = name_text;
    if (class_names_.count(name_text) != 0 && check(ast::TokenType::LAngle)) {
      type_or_name = parseTypeArgsSuffix(name_text);
    }

    if (check(ast::TokenType::LParen)) {
      if (class_names_.count(name_text) != 0) {
        auto expr = std::make_unique<ast::NewExpr>();
        expr->loc = name.loc;
        expr->class_name = type_or_name;
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

bool Parser::parseGenericParams(std::vector<ast::GenericParam>& out, bool allow_naked) {
  // Functions: <T: IReader>. Classes/containers may use naked <T>.
  consume(ast::TokenType::LAngle, "expected '<' to start generic parameters");
  for (;;) {
    ast::Token tname = consume(ast::TokenType::Identifier, "expected generic type name");
    ast::GenericParam gp;
    gp.loc = tname.loc;
    gp.name = tname.lexeme;
    if (match(ast::TokenType::Colon)) {
      ast::Token constraint =
          consume(ast::TokenType::Identifier, "expected interface constraint after ':'");
      gp.constraint = constraint.lexeme;
    } else if (!allow_naked) {
      errorAt(peek(), "expected ':' after generic name (naked <T> is forbidden)");
    }
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
    parseGenericParams(func->generics, /*allow_naked=*/false);
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

ast::StmtPtr Parser::parseForInStmt() {
  // for item in collection { ... }
  ast::Token kw = consume(ast::TokenType::KwFor, "expected 'for'");
  ast::Token item = consume(ast::TokenType::Identifier, "expected loop variable after 'for'");
  consume(ast::TokenType::KwIn, "expected 'in' after for-loop variable");
  ast::ExprPtr iterable = parseExpression(0);
  auto stmt = std::make_unique<ast::ForInStmt>();
  stmt->loc = kw.loc;
  stmt->item_name = item.lexeme;
  stmt->iterable = std::move(iterable);
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

ast::StmtPtr Parser::parseImportStmt() {
  ast::Token kw = consume(ast::TokenType::KwImport, "expected 'import'");
  auto stmt = std::make_unique<ast::ImportStmt>();
  stmt->loc = kw.loc;
  if (check(ast::TokenType::StringLiteral)) {
    stmt->path = peek().lexeme;
    advance();
  } else if (check(ast::TokenType::Identifier)) {
    // import mod_a;  →  mod_a.l
    stmt->path = peek().lexeme + ".l";
    advance();
  } else {
    errorAt(peek(), "expected module path string or name after import");
  }
  consume(ast::TokenType::Semicolon, "expected ';' after import");
  return stmt;
}

ast::StmtPtr Parser::parsePubDecl() {
  consume(ast::TokenType::KwPub, "expected 'pub'");
  if (check(ast::TokenType::KwFun)) {
    auto fn = parseFunCommon(/*allow_body=*/true);
    fn->is_pub = true;
    return fn;
  }
  if (check(ast::TokenType::KwClass)) {
    auto cls = parseClassDecl();
    if (cls && cls->kind() == ast::StmtKind::ClassDecl) {
      static_cast<ast::ClassDeclStmt&>(*cls).is_pub = true;
    }
    return cls;
  }
  if (check(ast::TokenType::KwInterface)) {
    auto iface = parseInterfaceDecl();
    if (iface && iface->kind() == ast::StmtKind::InterfaceDecl) {
      static_cast<ast::InterfaceDeclStmt&>(*iface).is_pub = true;
    }
    return iface;
  }
  errorAt(peek(), "expected fun/class/interface after pub");
  return nullptr;
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

  const auto saved_generics = generic_type_names_;
  if (check(ast::TokenType::LAngle)) {
    // Containers may use naked <T>; constrained form still accepted.
    parseGenericParams(cls->generics, /*allow_naked=*/true);
  }

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
    if (check(ast::TokenType::KwPub) && peek(1).type == ast::TokenType::KwFun) {
      advance();  // pub
      auto method = parseFunCommon(/*allow_body=*/true);
      method->is_method = true;
      method->is_pub = true;
      cls->methods.push_back(std::move(method));
      continue;
    }
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
  generic_type_names_ = saved_generics;
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

std::size_t Parser::skipGenericArgs(std::size_t from) const {
  if (peek(from).type != ast::TokenType::LAngle) {
    return from;
  }
  int depth = 0;
  std::size_t i = from;
  while (true) {
    const ast::Token& tok = peek(i);
    if (tok.type == ast::TokenType::EndOfFile) {
      return i;
    }
    if (tok.type == ast::TokenType::LAngle) {
      ++depth;
    } else if (tok.type == ast::TokenType::RAngle) {
      --depth;
      ++i;
      if (depth == 0) {
        return i;
      }
      continue;
    }
    ++i;
  }
}

bool Parser::looksLikeTypedDecl() const {
  if (isBuiltinTypeToken(peek().type)) {
    return peek(1).type == ast::TokenType::Identifier;
  }
  if (peek().type == ast::TokenType::Identifier &&
      (class_names_.count(peek().lexeme) != 0 ||
       interface_names_.count(peek().lexeme) != 0)) {
    std::size_t after_type = 1;
    if (peek(1).type == ast::TokenType::LAngle) {
      after_type = skipGenericArgs(1);
    }
    if (peek(after_type).type != ast::TokenType::Identifier) {
      return false;
    }
    const auto next = peek(after_type + 1).type;
    return next == ast::TokenType::Equal || next == ast::TokenType::Bang;
  }
  return false;
}

std::string Parser::parseTypeArgsSuffix(std::string base) {
  consume(ast::TokenType::LAngle, "expected '<' after generic type");
  std::string out = std::move(base);
  out.push_back('<');
  bool first = true;
  for (;;) {
    if (!first) {
      out.push_back(',');
    }
    first = false;
    std::string arg = parseTypeName();
    if (arg.empty()) {
      errorAt(peek(), "expected type argument");
      arg = "?";
    }
    out += arg;
    if (!match(ast::TokenType::Comma)) {
      break;
    }
  }
  consume(ast::TokenType::RAngle, "expected '>' after type arguments");
  out.push_back('>');
  return out;
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
    if (check(ast::TokenType::LAngle)) {
      return parseTypeArgsSuffix(std::move(name));
    }
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
