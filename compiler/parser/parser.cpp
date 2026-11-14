#include "parser/parser.hpp"

#include <sstream>
#include <utility>

#include "comptime/interpreter.hpp"
#include "comptime/snippet.hpp"
#include "lexer/lexer.hpp"

namespace l::parser {

namespace {
constexpr const char* kOnClassParsed = "__on_class_parsed";
}  // namespace

Parser::Parser(std::vector<ast::Token> tokens, diag::DiagnosticEngine* diags)
    : tokens_(std::move(tokens)), diags_(diags) {
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

void Parser::addAstHook(AstHook hook) {
  ast_hooks_.push_back(std::move(hook));
}

void Parser::pushContext(const std::string& name) {
  if (!name.empty()) {
    context_path_.push_back(name);
  }
}

void Parser::popContext() {
  if (!context_path_.empty()) {
    context_path_.pop_back();
  }
}

std::string Parser::currentContextPath() const {
  std::string out;
  for (std::size_t i = 0; i < context_path_.size(); ++i) {
    if (i != 0) {
      out.push_back('.');
    }
    out += context_path_[i];
  }
  return out;
}

void Parser::noteParsedStmt(ast::Stmt& stmt) {
  if (stmt.kind() == ast::StmtKind::FuncDecl) {
    auto& fn = static_cast<ast::FuncDeclStmt&>(stmt);
    comptime_funcs_[fn.name] = &fn;
  }
}

void Parser::flushPendingInjections(ast::Module& module) {
  for (ast::StmtPtr& inj : pending_injections_) {
    if (!inj) {
      continue;
    }
    noteParsedStmt(*inj);
    module.stmts.push_back(std::move(inj));
  }
  pending_injections_.clear();
}

void Parser::maybeRunClassHook(ast::ClassDeclStmt& cls) {
  // Fast path: disabled → no interpreter lookup or call ([hook-performance]).
  if (!class_hook_enabled_) {
    return;
  }
  auto it = comptime_funcs_.find(kOnClassParsed);
  if (it == comptime_funcs_.end() || !it->second) {
    had_error_ = true;
    if (diags_) {
      diags_->error(cls.loc, std::string("class hook enabled but missing pub fun ") +
                                 kOnClassParsed + "(String className) -> String");
    }
    return;
  }
  comptime::Interpreter interp(diags_);
  interp.setSourceContext(cls.loc);
  interp.setFuncContext(kOnClassParsed);
  comptime::Value result =
      interp.callFunction(*it->second, {comptime::Value::makeString(cls.name)});
  if (result.kind != comptime::ValueKind::String) {
    if (diags_) {
      diags_->error(cls.loc, "class hook must return String");
    }
    had_error_ = true;
    return;
  }
  if (result.s.empty()) {
    return;
  }
  if (!diags_) {
    return;
  }
  std::vector<ast::StmtPtr> pieces =
      comptime::parseSnippet(result.s, cls.loc, *diags_);
  for (ast::StmtPtr& p : pieces) {
    pending_injections_.push_back(std::move(p));
  }
}

std::unique_ptr<ast::Module> Parser::parseModule() {
  auto module = std::make_unique<ast::Module>();
  while (!atEnd()) {
    ast::StmtPtr stmt = parseStatement();
    if (stmt) {
      noteParsedStmt(*stmt);
      // Module lifecycle hooks live on Module, not as ordinary top-level callables.
      if (stmt->kind() == ast::StmtKind::FuncDecl) {
        auto* fn = static_cast<ast::FuncDeclStmt*>(stmt.get());
        if (fn->name == "__init" || fn->name == "__deinit") {
          if (!fn->params.empty()) {
            if (diags_) {
              diags_->error(fn->loc, fn->name + " must take no parameters");
            }
            had_error_ = true;
          }
          if (!fn->return_type.empty() && fn->return_type != "void") {
            if (diags_) {
              diags_->error(fn->loc, fn->name + " must return void");
            }
            had_error_ = true;
          }
          if (!fn->generics.empty()) {
            if (diags_) {
              diags_->error(fn->loc, fn->name + " cannot be generic");
            }
            had_error_ = true;
          }
          std::unique_ptr<ast::FuncDeclStmt> hook(
              static_cast<ast::FuncDeclStmt*>(stmt.release()));
          if (hook->name == "__init") {
            module->init_hooks.push_back(std::move(hook));
          } else {
            module->deinit_hooks.push_back(std::move(hook));
          }
          flushPendingInjections(*module);
          continue;
        }
      }
      module->stmts.push_back(std::move(stmt));
      flushPendingInjections(*module);
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
  if (check(ast::TokenType::LcmdDirective)) {
    return parseDirective();
  }
  if (check(ast::TokenType::KwImport)) {
    return parseImportStmt();
  }
  if (check(ast::TokenType::KwExtern)) {
    return parseExternModule();
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
  if (check(ast::TokenType::KwPartial)) {
    return parseClassDecl();
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
  if (check(ast::TokenType::KwMatch)) {
    return parseMatchStmt();
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
  if (check(ast::TokenType::KwSpawn)) {
    return parseSpawnBlock();
  }
  if (check(ast::TokenType::KwComptime)) {
    return parseComptimeBlock();
  }
  if (check(ast::TokenType::KwConst)) {
    return parseConstDecl();
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
  if (looksLikeDestructureDecl()) {
    return parseDestructureDecl();
  }
  if (check(ast::TokenType::Identifier) || check(ast::TokenType::IntLiteral) ||
      check(ast::TokenType::FloatLiteral) || check(ast::TokenType::StringLiteral) ||
      check(ast::TokenType::TyString) || check(ast::TokenType::LParen) ||
      check(ast::TokenType::LBracket) || check(ast::TokenType::KwTrue) ||
      check(ast::TokenType::KwFalse) || check(ast::TokenType::KwHeap) ||
      check(ast::TokenType::KwArena) || check(ast::TokenType::PlusPlus) ||
      check(ast::TokenType::MinusMinus) ||
      // Relative-init: `.baud = …` inside `obj { ... }`.
      (!relative_bases_.empty() && check(ast::TokenType::Dot))) {
    return parseAssignOrExprStmt();
  }
  return nullptr;
}

ast::ExprPtr Parser::parseUnaryExpression() {
  // Prefix ++ / -- : `++a!` / `--a!` (mutable `!` required after target).
  if (check(ast::TokenType::PlusPlus) || check(ast::TokenType::MinusMinus)) {
    const bool is_inc = check(ast::TokenType::PlusPlus);
    ast::Token op = peek();
    advance();
    ast::ExprPtr target = parsePrimaryExpression();
    if (!target) {
      return nullptr;
    }
    // Allow member/index postfix on the target before the required `!`.
    target = parsePostfix(std::move(target));
    if (!match(ast::TokenType::Bang)) {
      errorAt(peek(), "increment/decrement requires mutable '!' (e.g. ++a!)");
    }
    auto expr = std::make_unique<ast::IncDecExpr>();
    expr->loc = op.loc;
    expr->is_prefix = true;
    expr->is_inc = is_inc;
    expr->target = std::move(target);
    return expr;
  }
  // Logical not: `!expr` (desugar to `expr == false`). Higher than infix, lower than postfix.
  if (check(ast::TokenType::Bang)) {
    ast::Token bang = peek();
    advance();
    ast::ExprPtr operand = parseUnaryExpression();
    if (!operand) {
      return nullptr;
    }
    auto lit = std::make_unique<ast::BooleanLiteralExpr>();
    lit->loc = bang.loc;
    lit->value = false;
    auto binary = std::make_unique<ast::BinaryExpr>();
    binary->loc = bang.loc;
    binary->op = "==";
    binary->left = std::move(operand);
    binary->right = std::move(lit);
    return binary;
  }
  // Address-of: `&expr` → ptr (lvalue storage address).
  if (check(ast::TokenType::Amp)) {
    ast::Token amp = peek();
    advance();
    ast::ExprPtr operand = parseUnaryExpression();
    if (!operand) {
      return nullptr;
    }
    auto expr = std::make_unique<ast::AddrOfExpr>();
    expr->loc = amp.loc;
    expr->operand = std::move(operand);
    return expr;
  }
  ast::ExprPtr left = parsePrimaryExpression();
  if (!left) {
    return nullptr;
  }
  return parsePostfix(std::move(left));
}

ast::ExprPtr Parser::parseExpression(int min_precedence) {
  ast::ExprPtr left = parseUnaryExpression();
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
    if (op_tok.type == ast::TokenType::QuestionQuestion) {
      auto coalesce = std::make_unique<ast::CoalesceExpr>();
      coalesce->loc = op_tok.loc;
      coalesce->left = std::move(binary->left);
      coalesce->right = std::move(binary->right);
      left = std::move(coalesce);
    } else {
      left = std::move(binary);
    }
  }

  return left;
}

void Parser::attachTrailingBlockIfPresent(std::vector<ast::CallArg>& args,
                                          bool allow_bare_brace) {
  if (!check(ast::TokenType::LBrace)) {
    return;
  }
  // Method trailing without parens still requires `{ |` ([config-syntax] relative-init).
  // Free calls `delay(100) { ... }` allow a bare brace as a zero-arg block.
  if (!allow_bare_brace && peek(1).type != ast::TokenType::Pipe &&
      peek(1).type != ast::TokenType::PipePipe) {
    return;
  }
  auto lambda = parseTrailingBlock();
  if (!lambda) {
    return;
  }
  ast::CallArg arg;
  arg.loc = lambda->loc;
  arg.value = std::move(lambda);
  args.push_back(std::move(arg));
}

void Parser::parseLambdaParamList(ast::LambdaExpr& lambda) {
  if (!check(ast::TokenType::Pipe)) {
    for (;;) {
      ast::Token p = consume(ast::TokenType::Identifier, "expected parameter name");
      lambda.params.push_back(p.lexeme);
      if (!match(ast::TokenType::Comma)) {
        break;
      }
    }
  }
  consume(ast::TokenType::Pipe, "expected '|' after lambda params");
}

std::unique_ptr<ast::LambdaExpr> Parser::parseTrailingBlock() {
  ast::Token brace = consume(ast::TokenType::LBrace, "expected '{'");
  auto lambda = std::make_unique<ast::LambdaExpr>();
  lambda->loc = brace.loc;

  // `{ |x| ... }` / `{ || ... }` / zero-arg `{ ... }` (e.g. delay(100) { ... }).
  if (check(ast::TokenType::PipePipe)) {
    advance();
  } else if (check(ast::TokenType::Pipe)) {
    advance();
    parseLambdaParamList(*lambda);
  }

  while (!atEnd() && !check(ast::TokenType::RBrace)) {
    ast::StmtPtr stmt = parseStatement();
    if (stmt) {
      lambda->body.push_back(std::move(stmt));
      continue;
    }
    if (had_error_) {
      synchronize();
      continue;
    }
    errorAt(peek(), "expected statement in trailing block");
    synchronize();
  }
  consume(ast::TokenType::RBrace, "expected '}' after trailing block");
  return lambda;
}

ast::ExprPtr Parser::parseLambdaExpr() {
  auto lambda = std::make_unique<ast::LambdaExpr>();
  if (check(ast::TokenType::PipePipe)) {
    // Empty-param sugar: `|| a * 2` (lexer fused the two '|').
    lambda->loc = peek().loc;
    advance();
  } else {
    ast::Token pipe = consume(ast::TokenType::Pipe, "expected '|' to start lambda");
    lambda->loc = pipe.loc;
    parseLambdaParamList(*lambda);
  }

  if (check(ast::TokenType::LBrace)) {
    advance();
    while (!atEnd() && !check(ast::TokenType::RBrace)) {
      ast::StmtPtr stmt = parseStatement();
      if (stmt) {
        lambda->body.push_back(std::move(stmt));
        continue;
      }
      if (had_error_) {
        synchronize();
        continue;
      }
      errorAt(peek(), "expected statement in lambda body");
      synchronize();
    }
    consume(ast::TokenType::RBrace, "expected '}' after lambda body");
    return lambda;
  }

  // Expression body: `|x| x * 2` / `|| a * 2` → `return ...`
  ast::ExprPtr value = parseExpression(0);
  if (!value) {
    return nullptr;
  }
  auto ret = std::make_unique<ast::ReturnStmt>();
  ret->loc = value->loc;
  ret->value = std::move(value);
  lambda->body.push_back(std::move(ret));
  return lambda;
}

ast::ExprPtr Parser::parseArrayLiteral() {
  ast::Token lbr = consume(ast::TokenType::LBracket, "expected '['");
  auto lit = std::make_unique<ast::ArrayLiteralExpr>();
  lit->loc = lbr.loc;
  if (!check(ast::TokenType::RBracket)) {
    for (;;) {
      ast::ExprPtr elem = parseExpression(0);
      if (!elem) {
        break;
      }
      lit->elements.push_back(std::move(elem));
      if (!match(ast::TokenType::Comma)) {
        break;
      }
    }
  }
  consume(ast::TokenType::RBracket, "expected ']' after array literal");
  return lit;
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
        attachTrailingBlockIfPresent(call->args);
        left = std::move(call);
        continue;
      }
      // `obj.each { |x| ... }` — method call with only a trailing block.
      if (check(ast::TokenType::LBrace) && peek(1).type == ast::TokenType::Pipe) {
        auto call = std::make_unique<ast::MethodCallExpr>();
        call->loc = field.loc;
        call->object = std::move(left);
        call->method = field.lexeme;
        attachTrailingBlockIfPresent(call->args);
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
    // `val as Type`
    if (match(ast::TokenType::KwAs)) {
      auto cast = std::make_unique<ast::CastExpr>();
      cast->loc = left->loc;
      cast->value = std::move(left);
      if (!isTypeStart()) {
        errorAt(peek(), "expected type after 'as'");
        return cast;
      }
      cast->type_name = parseTypeName();
      left = std::move(cast);
      continue;
    }
    // Postfix `a!++` / `a!--` — only when `!` is followed by ++/--
    // (so `a! = 1` / `a! += 1` still use statement-level Bang).
    if (check(ast::TokenType::Bang) &&
        (peek(1).type == ast::TokenType::PlusPlus ||
         peek(1).type == ast::TokenType::MinusMinus)) {
      advance();  // !
      const bool is_inc = check(ast::TokenType::PlusPlus);
      ast::Token op = peek();
      advance();  // ++ / --
      auto expr = std::make_unique<ast::IncDecExpr>();
      expr->loc = op.loc;
      expr->is_prefix = false;
      expr->is_inc = is_inc;
      expr->target = std::move(left);
      left = std::move(expr);
      continue;
    }
    // `expr?` — Result error propagation (TryExpr).
    if (match(ast::TokenType::Question)) {
      auto try_expr = std::make_unique<ast::TryExpr>();
      try_expr->loc = left->loc;
      try_expr->inner = std::move(left);
      left = std::move(try_expr);
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

bool Parser::maybeConsumeStmtSemicolon() {
  if (match(ast::TokenType::Semicolon)) {
    return true;
  }
  // Trailing-block exprs end with '}'; ASI after '}' is suppressed ([asi]), so
  // allow omitting ';' when the next token starts another statement / ends the block.
  if (check(ast::TokenType::RBrace) || check(ast::TokenType::EndOfFile) ||
      check(ast::TokenType::KwReturn) || check(ast::TokenType::KwIf) ||
      check(ast::TokenType::KwMatch) || check(ast::TokenType::KwWhile) ||
      check(ast::TokenType::KwFor) || check(ast::TokenType::KwDefer) ||
      check(ast::TokenType::KwSync) || check(ast::TokenType::KwSpawn) ||
      check(ast::TokenType::KwConst) || check(ast::TokenType::KwConstd) ||
      check(ast::TokenType::KwFun) || check(ast::TokenType::KwClass) ||
      check(ast::TokenType::KwPartial) || check(ast::TokenType::KwPub) ||
      check(ast::TokenType::KwImport) || check(ast::TokenType::LcmdDirective) ||
      check(ast::TokenType::Identifier) || looksLikeTypedDecl()) {
    return true;
  }
  errorAt(peek(), "expected ';' after expression");
  return false;
}

int Parser::infixPrecedence(ast::TokenType type) {
  switch (type) {
    case ast::TokenType::QuestionQuestion:  // `opt ?? fallback` binds loosest
      return 0;
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
    case ast::TokenType::DotDot:  // `0..n` range (for-in)
      return 5;
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

std::string Parser::unescapeStringFragment(const std::string& raw) {
  // Lexer already expands \n \r \t \" \\. Remaining `\<c>` (e.g. `\{`) stay as-is.
  return raw;
}

ast::ExprPtr Parser::parseInterpFragment(const std::string& src,
                                         const ast::SourceLocation& loc) {
  // Speculative parse: no DiagnosticEngine so LCMD / code-in-string `{...}`
  // failures can fall back to a literal without polluting diagnostics.
  l::lexer::Lexer lexer(loc.filename.empty() ? std::string("<interp>") : loc.filename, src);
  std::vector<ast::Token> tokens = lexer.tokenize();
  Parser sub(std::move(tokens), nullptr);
  for (const std::string& c : class_names_) {
    sub.registerClassName(c);
  }
  for (const std::string& i : interface_names_) {
    sub.registerInterfaceName(i);
  }
  ast::ExprPtr expr = sub.parseExpression(0);
  if (!expr || sub.hadError()) {
    return nullptr;
  }
  // Expression only — reject leftover statements; allow ASI trailing `;`.
  if (!sub.onlyTrailingSemicolonsLeft()) {
    return nullptr;
  }
  expr->loc = loc;
  return expr;
}

ast::ExprPtr Parser::parseStringLiteralOrInterp() {
  ast::Token tok = consume(ast::TokenType::StringLiteral, "expected string literal");
  const std::string& raw = tok.lexeme;

  // Fast path: no unescaped `{` → plain string.
  bool has_interp = false;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\\' && i + 1 < raw.size()) {
      ++i;
      continue;
    }
    if (raw[i] == '{') {
      has_interp = true;
      break;
    }
  }
  if (!has_interp) {
    auto expr = std::make_unique<ast::StringLiteralExpr>();
    expr->loc = tok.loc;
    expr->value = unescapeStringFragment(raw);
    return expr;
  }

  // Split into literal / {expr} parts and fold with `+`.
  std::vector<ast::ExprPtr> parts;
  std::string lit;
  for (std::size_t i = 0; i < raw.size();) {
    if (raw[i] == '\\' && i + 1 < raw.size()) {
      lit.push_back('\\');
      lit.push_back(raw[i + 1]);
      i += 2;
      continue;
    }
    if (raw[i] != '{') {
      lit.push_back(raw[i]);
      ++i;
      continue;
    }
    if (!lit.empty()) {
      auto s = std::make_unique<ast::StringLiteralExpr>();
      s->loc = tok.loc;
      s->value = unescapeStringFragment(lit);
      parts.push_back(std::move(s));
      lit.clear();
    }
    ++i;  // skip '{'
    const std::size_t start = i;
    int depth = 1;
    while (i < raw.size() && depth > 0) {
      if (raw[i] == '\\' && i + 1 < raw.size()) {
        i += 2;
        continue;
      }
      if (raw[i] == '{') {
        ++depth;
      } else if (raw[i] == '}') {
        --depth;
      }
      if (depth > 0) {
        ++i;
      }
    }
    if (depth != 0) {
      errorAt(tok, "unclosed '{' in string interpolation");
      return nullptr;
    }
    const std::string frag = raw.substr(start, i - start);
    ++i;  // skip '}'
    ast::ExprPtr inner = parseInterpFragment(frag, tok.loc);
    if (!inner) {
      // Not a valid expression (e.g. `"…{ return 42; }"` from LCMD) → literal braces.
      lit.append("{");
      lit.append(frag);
      lit.push_back('}');
      continue;
    }
    parts.push_back(std::move(inner));
  }
  if (!lit.empty() || parts.empty()) {
    auto s = std::make_unique<ast::StringLiteralExpr>();
    s->loc = tok.loc;
    s->value = unescapeStringFragment(lit);
    parts.push_back(std::move(s));
  }

  ast::ExprPtr acc = std::move(parts[0]);
  for (std::size_t p = 1; p < parts.size(); ++p) {
    auto bin = std::make_unique<ast::BinaryExpr>();
    bin->loc = tok.loc;
    bin->op = "+";
    bin->left = std::move(acc);
    bin->right = std::move(parts[p]);
    acc = std::move(bin);
  }
  return acc;
}

ast::ExprPtr Parser::parsePrimaryExpression() {
  if (check(ast::TokenType::Invalid)) {
    errorAt(peek(), "expected expression");
    advance();
    return nullptr;
  }
  // Relative-init context: leading `.baud` → `base.baud` (property sugar later).
  if (!relative_bases_.empty() && check(ast::TokenType::Dot) &&
      peek(1).type == ast::TokenType::Identifier) {
    ast::Token dot = peek();
    advance();  // .
    ast::Token field = consume(ast::TokenType::Identifier, "expected name after '.'");
    auto access = std::make_unique<ast::MemberAccessExpr>();
    access->loc = field.loc;
    access->object = cloneExpr(*relative_bases_.back());
    if (!access->object) {
      errorAt(dot, "relative '.' requires a cloneable base expression");
      return nullptr;
    }
    access->member = field.lexeme;
    return parsePostfix(std::move(access));
  }
  if (check(ast::TokenType::Pipe) || check(ast::TokenType::PipePipe)) {
    // `|| expr` is lexed as PipePipe (same glyph as logical-or); treat as empty-param lambda.
    return parseLambdaExpr();
  }
  if (check(ast::TokenType::LBracket)) {
    return parseArrayLiteral();
  }
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
  // `arena(size)` — bump-pool constructor (builtin CallExpr).
  if (check(ast::TokenType::KwArena)) {
    ast::Token kw = peek();
    advance();
    if (!check(ast::TokenType::LParen)) {
      errorAt(peek(), "expected '(' after arena");
      return nullptr;
    }
    return parseCallArgs("arena", kw.loc);
  }
  if (check(ast::TokenType::IntLiteral)) {
    ast::Token tok = peek();
    advance();
    auto expr = std::make_unique<ast::IntLiteralExpr>();
    expr->loc = tok.loc;
    expr->value = tok.lexeme;
    return expr;
  }
  if (check(ast::TokenType::FloatLiteral)) {
    ast::Token tok = peek();
    advance();
    auto expr = std::make_unique<ast::FloatLiteralExpr>();
    expr->loc = tok.loc;
    expr->value = tok.lexeme;
    return expr;
  }
  if (check(ast::TokenType::StringLiteral)) {
    return parseStringLiteralOrInterp();
  }
  if (check(ast::TokenType::KwTrue) || check(ast::TokenType::KwFalse)) {
    ast::Token tok = peek();
    advance();
    auto expr = std::make_unique<ast::BooleanLiteralExpr>();
    expr->loc = tok.loc;
    expr->value = (tok.type == ast::TokenType::KwTrue);
    return expr;
  }
  // `spawn { ... }` expression → opaque thread handle (`ptr`).
  if (check(ast::TokenType::KwSpawn)) {
    ast::Token kw = consume(ast::TokenType::KwSpawn, "expected 'spawn'");
    auto expr = std::make_unique<ast::SpawnExpr>();
    expr->loc = kw.loc;
    expr->body = parseBlockBody();
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
  // Bare `{ ... }` after a call would steal `if foo() {` bodies. Only `delay(ms) {`
  // opts into zero-arg trailing without `|`; others still need `{ |` / `{||`.
  attachTrailingBlockIfPresent(call->args, /*allow_bare_brace=*/call->callee == "delay");
  return call;
}

ast::StmtPtr Parser::parseConstDecl() {
  ast::Token kw = consume(ast::TokenType::KwConst, "expected 'const'");
  ast::Token name = consume(ast::TokenType::Identifier, "expected name after const");
  consume(ast::TokenType::Equal, "expected '=' after const name");
  ast::ExprPtr init = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after const declaration");

  auto stmt = std::make_unique<ast::VarDeclStmt>();
  stmt->loc = kw.loc;
  stmt->decl_kind = ast::VarDeclKind::Const;
  stmt->name = name.lexeme;
  stmt->init = std::move(init);
  return stmt;
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
  // Allow omitting ';' when the initializer ends with `}` (e.g. `h := spawn { ... }`).
  maybeConsumeStmtSemicolon();

  auto stmt = std::make_unique<ast::VarDeclStmt>();
  stmt->loc = name.loc;
  stmt->decl_kind = ast::VarDeclKind::Infer;
  stmt->name = name.lexeme;
  stmt->is_mutable = is_mutable;
  stmt->init = std::move(init);
  return stmt;
}

bool Parser::looksLikeDestructureDecl() const {
  // `(a, b) :=` / `(a!, b!) :=` — at least one comma inside parens, then `:=`.
  if (!check(ast::TokenType::LParen)) {
    return false;
  }
  std::size_t i = 1;
  if (peek(i).type != ast::TokenType::Identifier) {
    return false;
  }
  ++i;
  if (peek(i).type == ast::TokenType::Bang) {
    ++i;
  }
  if (peek(i).type != ast::TokenType::Comma) {
    return false;
  }
  int depth = 1;
  while (i < tokens_.size() && depth > 0) {
    const ast::TokenType t = peek(i).type;
    if (t == ast::TokenType::LParen) {
      ++depth;
    } else if (t == ast::TokenType::RParen) {
      --depth;
    } else if (t == ast::TokenType::EndOfFile) {
      return false;
    }
    ++i;
  }
  if (depth != 0) {
    return false;
  }
  return peek(i).type == ast::TokenType::ColonAssign;
}

ast::StmtPtr Parser::parseDestructureDecl() {
  ast::Token lparen = consume(ast::TokenType::LParen, "expected '(' for destructure");
  auto stmt = std::make_unique<ast::DestructureDeclStmt>();
  stmt->loc = lparen.loc;

  do {
    ast::Token name = consume(ast::TokenType::Identifier, "expected binding name in destructure");
    ast::DestructureBinding b;
    b.name = name.lexeme;
    b.loc = name.loc;
    b.is_mutable = match(ast::TokenType::Bang);
    stmt->bindings.push_back(std::move(b));
  } while (match(ast::TokenType::Comma));

  consume(ast::TokenType::RParen, "expected ')' after destructure bindings");
  consume(ast::TokenType::ColonAssign, "expected ':=' after destructure bindings");
  stmt->init = parseExpression();
  consume(ast::TokenType::Semicolon, "expected ';' after destructure declaration");

  if (stmt->bindings.size() < 2) {
    errorAt(lparen, "destructure requires at least two bindings");
  }
  return stmt;
}

ast::ExprPtr Parser::cloneExpr(const ast::Expr& expr) {
  switch (expr.kind()) {
    case ast::ExprKind::Identifier: {
      const auto& id = static_cast<const ast::IdentifierExpr&>(expr);
      auto out = std::make_unique<ast::IdentifierExpr>();
      out->loc = id.loc;
      out->name = id.name;
      return out;
    }
    case ast::ExprKind::MemberAccess: {
      const auto& m = static_cast<const ast::MemberAccessExpr&>(expr);
      auto out = std::make_unique<ast::MemberAccessExpr>();
      out->loc = m.loc;
      out->member = m.member;
      if (m.object) {
        out->object = cloneExpr(*m.object);
      }
      return out;
    }
    case ast::ExprKind::Index: {
      const auto& ix = static_cast<const ast::IndexExpr&>(expr);
      auto out = std::make_unique<ast::IndexExpr>();
      out->loc = ix.loc;
      if (ix.object) {
        out->object = cloneExpr(*ix.object);
      }
      if (ix.index) {
        out->index = cloneExpr(*ix.index);
      }
      return out;
    }
    default:
      return nullptr;
  }
}

ast::StmtPtr Parser::parseRelativeAssignBlock(ast::ExprPtr base) {
  ast::Token brace = consume(ast::TokenType::LBrace, "expected '{' after relative-init base");
  auto block = std::make_unique<ast::BlockStmt>();
  block->loc = brace.loc;

  relative_bases_.push_back(std::move(base));
  while (!atEnd() && !check(ast::TokenType::RBrace)) {
    ast::StmtPtr stmt = parseStatement();
    if (stmt) {
      block->stmts.push_back(std::move(stmt));
      continue;
    }
    if (had_error_) {
      synchronize();
      continue;
    }
    errorAt(peek(), "expected statement in relative-init block");
    synchronize();
  }
  consume(ast::TokenType::RBrace, "expected '}' after relative-init block");
  relative_bases_.pop_back();
  maybeConsumeStmtSemicolon();
  return block;
}

ast::StmtPtr Parser::parseAssignOrExprStmt() {
  ast::Token start = peek();
  ast::ExprPtr expr = parseExpression(0);
  if (!expr) {
    return nullptr;
  }

  // `app.rs232 { .baud = 115200 }` — statement-position relative init (not if/while).
  if (check(ast::TokenType::LBrace) && peek(1).type == ast::TokenType::Dot) {
    return parseRelativeAssignBlock(std::move(expr));
  }

  // Mutable marker must be seen before '=' / '+=' / ... (e.g. `a! += 1`).
  const bool mutable_marker = match(ast::TokenType::Bang);

  std::string compound_op;
  if (match(ast::TokenType::Equal)) {
    compound_op.clear();
  } else if (match(ast::TokenType::PlusEqual)) {
    compound_op = "+";
  } else if (match(ast::TokenType::MinusEqual)) {
    compound_op = "-";
  } else if (match(ast::TokenType::StarEqual)) {
    compound_op = "*";
  } else if (match(ast::TokenType::SlashEqual)) {
    compound_op = "/";
  } else {
    if (mutable_marker) {
      errorAt(peek(), "'!' is only valid on mutable assignment LHS");
    }
    maybeConsumeStmtSemicolon();
    auto stmt = std::make_unique<ast::ExprStmt>();
    stmt->loc = start.loc;
    stmt->expr = std::move(expr);
    return stmt;
  }

  ast::ExprPtr rhs = parseExpression(0);
  consume(ast::TokenType::Semicolon, "expected ';' after assignment");

  ast::ExprPtr value = std::move(rhs);
  if (!compound_op.empty()) {
    ast::ExprPtr lhs_copy = cloneExpr(*expr);
    if (!lhs_copy) {
      errorAt(start, "compound assignment requires an identifier / member / index LHS");
      return nullptr;
    }
    auto bin = std::make_unique<ast::BinaryExpr>();
    bin->loc = start.loc;
    bin->op = compound_op;
    bin->left = std::move(lhs_copy);
    bin->right = std::move(value);
    value = std::move(bin);
  }

  auto stmt = std::make_unique<ast::AssignStmt>();
  stmt->loc = start.loc;
  stmt->target = std::move(expr);
  stmt->value = std::move(value);
  stmt->mutable_marker = mutable_marker;
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
  // Class/struct params: bare name is read-only ref; `name!` allows field mutation.
  param.is_mutable = match(ast::TokenType::Bang);
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
  pushContext(func->name);

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
      popContext();
      return func;
    }
    func->return_type = parseTypeName();
  } else {
    func->return_type = "void";
  }

  if (allow_body && check(ast::TokenType::LBrace)) {
    func->body = parseBlockBody();
  } else {
    // Signature-only (interface / extern). ASI often inserts ';' after type tokens.
    match(ast::TokenType::Semicolon);
  }

  generic_type_names_ = saved_generics;
  popContext();
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
  maybeConsumeStmtSemicolon();
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

ast::MatchBranch Parser::parseMatchBranch() {
  ast::MatchBranch branch;
  branch.loc = peek().loc;

  if (check(ast::TokenType::Identifier) && peek().lexeme == "_") {
    advance();
    branch.is_wildcard = true;
  } else if (check(ast::TokenType::IntLiteral)) {
    ast::Token tok = peek();
    advance();
    auto lit = std::make_unique<ast::IntLiteralExpr>();
    lit->loc = tok.loc;
    lit->value = tok.lexeme;
    branch.pattern = std::move(lit);
  } else if (check(ast::TokenType::StringLiteral)) {
    ast::Token tok = peek();
    advance();
    auto lit = std::make_unique<ast::StringLiteralExpr>();
    lit->loc = tok.loc;
    lit->value = tok.lexeme;
    branch.pattern = std::move(lit);
  } else if (check(ast::TokenType::KwTrue) || check(ast::TokenType::KwFalse)) {
    ast::Token tok = peek();
    advance();
    auto lit = std::make_unique<ast::BooleanLiteralExpr>();
    lit->loc = tok.loc;
    lit->value = tok.type == ast::TokenType::KwTrue;
    branch.pattern = std::move(lit);
  } else {
    errorAt(peek(), "expected match pattern (literal or '_')");
    synchronize();
    return branch;
  }

  consume(ast::TokenType::FatArrow, "expected '=>' after match pattern");
  branch.body = parseBlockBody();
  return branch;
}

ast::StmtPtr Parser::parseMatchStmt() {
  ast::Token kw = consume(ast::TokenType::KwMatch, "expected 'match'");
  ast::ExprPtr scrutinee = parseExpression(0);
  auto stmt = std::make_unique<ast::MatchStmt>();
  stmt->loc = kw.loc;
  stmt->scrutinee = std::move(scrutinee);
  consume(ast::TokenType::LBrace, "expected '{' after match expression");
  while (!atEnd() && !check(ast::TokenType::RBrace)) {
    // ASI may insert ';' between arms; skip them.
    if (match(ast::TokenType::Semicolon)) {
      continue;
    }
    stmt->branches.push_back(parseMatchBranch());
  }
  consume(ast::TokenType::RBrace, "expected '}' after match arms");
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

ast::StmtPtr Parser::parseSpawnBlock() {
  ast::Token kw = consume(ast::TokenType::KwSpawn, "expected 'spawn'");
  auto stmt = std::make_unique<ast::SpawnBlockStmt>();
  stmt->loc = kw.loc;
  stmt->body = parseBlockBody();
  return stmt;
}

ast::StmtPtr Parser::parseComptimeBlock() {
  ast::Token kw = consume(ast::TokenType::KwComptime, "expected 'comptime'");
  auto stmt = std::make_unique<ast::ComptimeStmt>();
  stmt->loc = kw.loc;
  stmt->body = parseBlockBody();
  return stmt;
}

ast::StmtPtr Parser::parseDirective() {
  ast::Token tok = consume(ast::TokenType::LcmdDirective, "expected LCMD directive");
  auto stmt = std::make_unique<ast::DirectiveStmt>();
  stmt->loc = tok.loc;

  // Lexeme is `cmd` or `cmd(args)` after the `LCMD_` prefix.
  const std::string& body = tok.lexeme;
  const auto lparen = body.find('(');
  if (lparen == std::string::npos) {
    stmt->command = body;
  } else {
    stmt->command = body.substr(0, lparen);
    if (!body.empty() && body.back() == ')') {
      stmt->args = body.substr(lparen + 1, body.size() - lparen - 2);
    } else {
      errorAt(tok, "LCMD directive missing closing ')'");
      stmt->args = body.substr(lparen + 1);
    }
  }
  // Optional ASI/explicit semicolon after the directive line.
  match(ast::TokenType::Semicolon);

  stmt->context_path = currentContextPath();

  // Opt-in class-hook toggles ([hook-performance]) — no interpreter cost by themselves.
  if (stmt->command == "enable_class_hook") {
    class_hook_enabled_ = true;
  } else if (stmt->command == "disable_class_hook") {
    class_hook_enabled_ = false;
  }

  for (AstHook& hook : ast_hooks_) {
    if (hook) {
      hook(*stmt);
    }
  }
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

ast::StmtPtr Parser::parseExternModule() {
  ast::Token kw = consume(ast::TokenType::KwExtern, "expected 'extern'");
  consume(ast::TokenType::KwModule, "expected 'module' after 'extern'");
  ast::Token name = consume(ast::TokenType::Identifier, "expected extern module name");
  consume(ast::TokenType::LBrace, "expected '{' after extern module name");

  auto stmt = std::make_unique<ast::ExternModuleStmt>();
  stmt->loc = kw.loc;
  stmt->name = name.lexeme;

  while (!atEnd() && !check(ast::TokenType::RBrace)) {
    if (!check(ast::TokenType::KwFun)) {
      errorAt(peek(), "expected function declaration in extern module");
      synchronize();
      continue;
    }
    auto fn = parseFunCommon(/*allow_body=*/false);
    fn->is_extern = true;
    fn->module_name = stmt->name;
    stmt->decls.push_back(std::move(fn));
  }

  consume(ast::TokenType::RBrace, "expected '}' after extern module body");
  return stmt;
}

ast::StmtPtr Parser::parsePubDecl() {
  consume(ast::TokenType::KwPub, "expected 'pub'");
  if (check(ast::TokenType::KwFun)) {
    auto fn = parseFunCommon(/*allow_body=*/true);
    fn->is_pub = true;
    return fn;
  }
  if (check(ast::TokenType::KwPartial) || check(ast::TokenType::KwClass)) {
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
  const bool is_partial = match(ast::TokenType::KwPartial);
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
  cls->is_partial = is_partial;
  class_names_.insert(class_name);
  pushContext(class_name);

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
    // Method forms (do not steal `pub Type field`):
    //   fun | pub fun | static fun | pub static fun | static pub fun
    const bool starts_method =
        check(ast::TokenType::KwFun) || check(ast::TokenType::KwStatic) ||
        (check(ast::TokenType::KwPub) &&
         (peek(1).type == ast::TokenType::KwFun || peek(1).type == ast::TokenType::KwStatic));
    if (starts_method) {
      bool method_pub = false;
      bool method_static = false;
      for (;;) {
        if (check(ast::TokenType::KwPub)) {
          advance();
          method_pub = true;
          continue;
        }
        if (check(ast::TokenType::KwStatic)) {
          advance();
          method_static = true;
          continue;
        }
        break;
      }
      if (!check(ast::TokenType::KwFun)) {
        errorAt(peek(), "expected 'fun' after method modifiers");
        synchronize();
        continue;
      }
      auto method = parseFunCommon(/*allow_body=*/true);
      method->is_method = true;
      method->is_pub = method_pub;
      method->is_static = method_static;
      cls->methods.push_back(std::move(method));
      continue;
    }
    // [delegate]: `delegate Engine` — anonymous field named after the type.
    if (check(ast::TokenType::KwDelegate)) {
      auto field = std::make_unique<ast::FieldDeclStmt>();
      field->loc = peek().loc;
      advance();  // delegate
      field->is_delegate = true;
      field->is_pub = true;  // promoted methods are reachable; type-named field is public
      if (!isTypeStart()) {
        errorAt(peek(), "expected type name after 'delegate'");
        synchronize();
        continue;
      }
      field->type_name = parseTypeName();
      field->name = field->type_name;
      consume(ast::TokenType::Semicolon, "expected ';' after delegate declaration");
      cls->fields.push_back(std::move(field));
      continue;
    }
    // `pub i32 level_` / `i32 score_`
    bool field_pub = false;
    if (check(ast::TokenType::KwPub)) {
      // Lookahead: pub + type starts a field (not handled above as pub fun).
      advance();
      field_pub = true;
    }
    if (!isTypeStart()) {
      errorAt(peek(), "expected field or method in class body");
      synchronize();
      continue;
    }
    auto field = std::make_unique<ast::FieldDeclStmt>();
    field->loc = peek().loc;
    field->is_pub = field_pub;
    field->type_name = parseTypeName();
    ast::Token field_name =
        consume(ast::TokenType::Identifier, "expected field name");
    field->name = field_name.lexeme;
    consume(ast::TokenType::Semicolon, "expected ';' after field declaration");
    cls->fields.push_back(std::move(field));
  }

  consume(ast::TokenType::RBrace, "expected '}' after class body");
  generic_type_names_ = saved_generics;
  popContext();

  // Only when explicitly enabled: run __on_class_parsed and queue AST injection.
  maybeRunClassHook(*cls);
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

bool Parser::onlyTrailingSemicolonsLeft() const {
  std::size_t i = index_;
  while (i < tokens_.size() && tokens_[i].type == ast::TokenType::Semicolon) {
    ++i;
  }
  return i < tokens_.size() && tokens_[i].type == ast::TokenType::EndOfFile;
}

void Parser::errorAt(const ast::Token& tok, const std::string& message) {
  had_error_ = true;
  std::ostringstream ss;
  if (tok.type == ast::TokenType::Invalid && tok.lexeme.size() > 1 && tok.lexeme.back() == '_') {
    ss << "identifier ending with '_' is forbidden: '" << tok.lexeme << "'";
  } else {
    ss << message << " (found " << ast::toString(tok.type) << ")";
  }
  if (diags_) {
    diags_->error(tok.loc, ss.str());
  }
}

void Parser::synchronize() {
  // Panic mode: discard tokens until a statement/block boundary, then resume.
  if (!atEnd()) {
    advance();  // always consume at least the offending token
  }
  while (!atEnd()) {
    if (peek().type == ast::TokenType::Semicolon) {
      advance();
      return;
    }
    if (peek().type == ast::TokenType::RBrace) {
      return;
    }
    // Declaration keywords are also safe resume points at module/block scope.
    switch (peek().type) {
      case ast::TokenType::KwFun:
      case ast::TokenType::KwClass:
      case ast::TokenType::KwInterface:
      case ast::TokenType::KwImport:
      case ast::TokenType::KwExtern:
      case ast::TokenType::KwPub:
      case ast::TokenType::KwReturn:
      case ast::TokenType::KwIf:
      case ast::TokenType::KwMatch:
      case ast::TokenType::KwWhile:
      case ast::TokenType::KwFor:
      case ast::TokenType::KwDefer:
      case ast::TokenType::KwSync:
      case ast::TokenType::KwSpawn:
        return;
      default:
        break;
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
  // Function type: fun(T) / fun(T)->void
  if (peek().type == ast::TokenType::KwFun) {
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
  // fun(T) or fun(T)->R — first-class function / callback type.
  if (check(ast::TokenType::KwFun)) {
    advance();
    consume(ast::TokenType::LParen, "expected '(' after fun type");
    std::string out = "fun(";
    if (!check(ast::TokenType::RParen)) {
      for (;;) {
        if (!isTypeStart() && !check(ast::TokenType::Identifier)) {
          errorAt(peek(), "expected parameter type in fun(...)");
          break;
        }
        out += parseTypeName();
        if (!match(ast::TokenType::Comma)) {
          break;
        }
        out += ",";
      }
    }
    consume(ast::TokenType::RParen, "expected ')' after fun parameter types");
    out += ")";
    if (match(ast::TokenType::Arrow)) {
      if (!isTypeStart() && !check(ast::TokenType::Identifier)) {
        errorAt(peek(), "expected return type after fun(...) ->");
        out += "->void";
      } else {
        out += "->";
        out += parseTypeName();
      }
    }
    return out;
  }
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
