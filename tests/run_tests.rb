#!/usr/bin/env ruby
# frozen_string_literal: true

# L test runner (Ruby automation only).

require "fileutils"
require "open3"
require "pathname"

ROOT = Pathname.new(__dir__).parent.expand_path
GOLDEN = ROOT.join("tests", "cases", "golden.l")
VAR_DECL = ROOT.join("tests", "cases", "var_decl.l")
EXPR = ROOT.join("tests", "cases", "expr.l")
FUNC_CALL = ROOT.join("tests", "cases", "func_call.l")
FUNC_DEF = ROOT.join("tests", "cases", "func_def.l")
CODEGEN_SIMPLE = ROOT.join("tests", "cases", "codegen_simple.l")
RUN_HELLO = ROOT.join("tests", "cases", "run_hello.l")
CONTROL_FLOW = ROOT.join("tests", "cases", "control_flow.l")
OUT_DIR = ROOT.join("build", "clang-debug", "test_out")
COMPILER = ENV.fetch(
  "L_COMPILER",
  ROOT.join("build", "clang-debug", "compiler", "l.exe").to_s
)

RED = "\e[31m"
GREEN = "\e[32m"
RESET = "\e[0m"

REQUIRED_TOKEN_TYPES = %w[
  KwConst
  KwConstd
  KwFun
  KwReturn
  KwInterface
  KwPartial
  KwClass
  TyI32
  TyVoid
  TyString
  Identifier
  IntLiteral
  StringLiteral
  Bang
  Equal
  ColonAssign
  Arrow
  Colon
  Semicolon
  Comma
  Dot
  Plus
  Pipe
  LParen
  RParen
  LBrace
  RBrace
  LAngle
  RAngle
  EndOfFile
].freeze

def color(text, code)
  "#{code}#{text}#{RESET}"
end

def fail_test!(msg)
  warn color("FAIL: #{msg}", RED)
  exit 1
end

def assert!(cond, msg)
  fail_test!(msg) unless cond
end

def run_compiler(*args)
  assert!(File.exist?(COMPILER), "compiler not found: #{COMPILER}")
  stdout, stderr, status = Open3.capture3(COMPILER, *args)
  [stdout, stderr, status]
end

def test_golden_dump_tokens!
  assert!(File.exist?(GOLDEN), "golden case not found: #{GOLDEN}")
  stdout, stderr, status = run_compiler("--dump-tokens", GOLDEN.to_s)
  assert!(status.success?, "dump-tokens exited #{status.exitstatus}\n#{stderr}")
  assert!(!stdout.strip.empty?, "dump-tokens produced empty stdout")

  first = stdout.each_line.map(&:rstrip).find { |l| !l.empty? }
  assert!(!first.nil?, "no token lines in dump-tokens output")
  m = first.match(/:(\d+):(\d+)\s*\z/)
  assert!(!m.nil?, "cannot parse SourceLocation from first token line: #{first}")
  assert!(
    m[1].to_i == 2 && m[2].to_i == 1,
    "first token SourceLocation expected 2:1, got #{m[1]}:#{m[2]} (#{first})"
  )

  missing = REQUIRED_TOKEN_TYPES.reject { |t| stdout.match?(/^\s*#{Regexp.escape(t)}\b/) }
  assert!(missing.empty?, "dump-tokens missing token types: #{missing.join(', ')}")
  puts "  OK golden --dump-tokens"
end

def test_var_decl_dump_ast!
  assert!(File.exist?(VAR_DECL), "var_decl case not found: #{VAR_DECL}")
  stdout, stderr, status = run_compiler("--dump-ast", VAR_DECL.to_s)
  assert!(status.success?, "dump-ast exited #{status.exitstatus}\n#{stderr}\n#{stdout}")

  assert!(stdout.include?("VarDeclStmt"), "AST dump missing VarDeclStmt")
  assert!(stdout.include?("constd a"), "AST dump missing constd a declaration")
  assert!(stdout.include?("i32 b"), "AST dump missing i32 b declaration")
  assert!(stdout.include?(":= c"), "AST dump missing := c declaration")
  assert!(stdout.include?("IntLiteralExpr 10"), "AST dump missing IntLiteralExpr 10")
  assert!(stdout.include?("IntLiteralExpr 20"), "AST dump missing IntLiteralExpr 20")
  assert!(stdout.include?("IntLiteralExpr 30"), "AST dump missing IntLiteralExpr 30")
  puts "  OK var_decl --dump-ast"
end

def test_expr_dump_ast!
  assert!(File.exist?(EXPR), "expr case not found: #{EXPR}")
  stdout, stderr, status = run_compiler("--dump-ast", EXPR.to_s)
  assert!(status.success?, "dump-ast exited #{status.exitstatus}\n#{stderr}\n#{stdout}")

  # 1 + (2 * 3): BinaryExpr + with IntLiteral 1 and BinaryExpr *
  mul_prec = /
    VarDeclStmt\ i32\ a\n
    \s+BinaryExpr\ \+\n
    \s+IntLiteralExpr\ 1\n
    \s+BinaryExpr\ \*\n
    \s+IntLiteralExpr\ 2\n
    \s+IntLiteralExpr\ 3\n
  /x
  assert!(stdout.match?(mul_prec), "expected AST shape 1 + (2 * 3), got:\n#{stdout}")

  # (10 - 5) + 2: left-associative + over -
  add_assoc = /
    VarDeclStmt\ :=\ b\n
    \s+BinaryExpr\ \+\n
    \s+BinaryExpr\ -\n
    \s+IntLiteralExpr\ 10\n
    \s+IntLiteralExpr\ 5\n
    \s+IntLiteralExpr\ 2\n
  /x
  assert!(stdout.match?(add_assoc), "expected AST shape (10 - 5) + 2, got:\n#{stdout}")

  puts "  OK expr --dump-ast"
end

def test_func_call_dump_ast!
  assert!(File.exist?(FUNC_CALL), "func_call case not found: #{FUNC_CALL}")
  stdout, stderr, status = run_compiler("--dump-ast", FUNC_CALL.to_s)
  assert!(status.success?, "dump-ast exited #{status.exitstatus}\n#{stderr}\n#{stdout}")

  # *(+(1, 2), 3) — parentheses force + under *
  paren_mul = /
    VarDeclStmt\ i32\ x\n
    \s+BinaryExpr\ \*\n
    \s+BinaryExpr\ \+\n
    \s+IntLiteralExpr\ 1\n
    \s+IntLiteralExpr\ 2\n
    \s+IntLiteralExpr\ 3\n
  /x
  assert!(stdout.match?(paren_mul), "expected AST shape *(+(1, 2), 3), got:\n#{stdout}")

  # CallExpr(add, [10, 20])
  call = /
    VarDeclStmt\ :=\ y\n
    \s+CallExpr\ add\ \[2\]\n
    \s+IntLiteralExpr\ 10\n
    \s+IntLiteralExpr\ 20\n
  /x
  assert!(stdout.match?(call), "expected CallExpr(add, [10, 20]), got:\n#{stdout}")

  puts "  OK func_call --dump-ast"
end

def test_func_def_dump_ast!
  assert!(File.exist?(FUNC_DEF), "func_def case not found: #{FUNC_DEF}")
  stdout, stderr, status = run_compiler("--dump-ast", FUNC_DEF.to_s)
  assert!(status.success?, "dump-ast exited #{status.exitstatus}\n#{stderr}\n#{stdout}")

  shape = /
    FuncDeclStmt\ add\ ->\ i32\n
    \s+Param\ i32\ a\n
    \s+Param\ i32\ b\n
    \s+ReturnStmt\n
    \s+BinaryExpr\ \+\n
    \s+IdentifierExpr\ a\n
    \s+IdentifierExpr\ b\n
  /x
  assert!(stdout.match?(shape), "expected FuncDeclStmt(add) with ReturnStmt +(a, b), got:\n#{stdout}")

  puts "  OK func_def --dump-ast"
end

def test_codegen_simple_emit_llvm!
  assert!(File.exist?(CODEGEN_SIMPLE), "codegen_simple case not found: #{CODEGEN_SIMPLE}")
  stdout, stderr, status = run_compiler("--emit-llvm", CODEGEN_SIMPLE.to_s)
  assert!(status.success?, "emit-llvm exited #{status.exitstatus}\n#{stderr}\n#{stdout}")

  assert!(stdout.include?("define i32 @main()"), "IR missing define i32 @main()\n#{stdout}")
  alloca_count = stdout.scan(/alloca i32/).size
  assert!(alloca_count >= 2, "expected >= 2 alloca i32 (x,y), got #{alloca_count}\n#{stdout}")
  assert!(stdout.match?(/store i32 10/), "IR missing store i32 10\n#{stdout}")
  assert!(stdout.match?(/store i32 20/), "IR missing store i32 20\n#{stdout}")
  assert!(stdout.match?(/\badd i32\b/), "IR missing add i32\n#{stdout}")
  assert!(stdout.match?(/\bret i32\b/), "IR missing ret i32\n#{stdout}")

  puts "  OK codegen_simple --emit-llvm"
end

def test_run_hello_e2e!
  assert!(File.exist?(RUN_HELLO), "run_hello case not found: #{RUN_HELLO}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("hello.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, RUN_HELLO.to_s)
  assert!(status.success?, "compile -o hello.exe failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "hello.exe was not produced at #{exe}")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "hello.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\n", "hello.exe output expected \"42\\n\", got #{normalized.inspect}")

  puts "  OK run_hello e2e"
end

def test_control_flow_e2e!
  assert!(File.exist?(CONTROL_FLOW), "control_flow case not found: #{CONTROL_FLOW}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("control_flow.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, CONTROL_FLOW.to_s)
  assert!(status.success?, "compile control_flow failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "control_flow.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "control_flow.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "999\n", "expected \"999\\n\", got #{normalized.inspect}")

  puts "  OK control_flow e2e"
end

def main
  test_golden_dump_tokens!
  test_var_decl_dump_ast!
  test_expr_dump_ast!
  test_func_call_dump_ast!
  test_func_def_dump_ast!
  test_codegen_simple_emit_llvm!
  test_run_hello_e2e!
  test_control_flow_e2e!

  puts color("PASS", GREEN)
  exit 0
end

main
