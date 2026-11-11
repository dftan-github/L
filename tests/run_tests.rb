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
CLASS_BASIC = ROOT.join("tests", "cases", "class_basic.l")
INTERFACE_TEST = ROOT.join("tests", "cases", "interface_test.l")
STRING_TEST = ROOT.join("tests", "cases", "string_test.l")
STDLIB_STRING = ROOT.join("stdlib", "string.l")
GENERIC_TEST = ROOT.join("tests", "cases", "generic_test.l")
HEAP_DEFER = ROOT.join("tests", "cases", "heap_defer.l")
SYNC_FAIL = ROOT.join("tests", "cases", "sync_fail.l")
SYNC_PASS = ROOT.join("tests", "cases", "sync_pass.l")
MAIN_IMPORT = ROOT.join("tests", "cases", "main_import.l")
MOD_A = ROOT.join("tests", "cases", "mod_a.l")
ARRAY_TEST = ROOT.join("tests", "cases", "array_test.l")
STDLIB_ARRAY = ROOT.join("stdlib", "array.l")
FOR_IN = ROOT.join("tests", "cases", "for_in.l")
GENERIC_INFERENCE = ROOT.join("tests", "cases", "generic_inference.l")
OPTIONAL_TEST = ROOT.join("tests", "cases", "optional_test.l")
STDLIB_OPTIONAL = ROOT.join("stdlib", "optional.l")
CONSTRUCTOR = ROOT.join("tests", "cases", "constructor.l")
STRING_CONCAT = ROOT.join("tests", "cases", "string_concat.l")
KW_MIXED = ROOT.join("tests", "cases", "kw_mixed.l")
KW_BAD = ROOT.join("tests", "cases", "kw_bad.l")
INPUT_TEST = ROOT.join("tests", "cases", "input_test.l")
STDLIB_IO = ROOT.join("stdlib", "io.l")
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

  # CallExpr(add, [10, 20]) — args wrapped as CallArg (positional)
  call = /
    VarDeclStmt\ :=\ y\n
    \s+CallExpr\ add\ \[2\]\n
    \s+CallArg\ \(positional\)\n
    \s+IntLiteralExpr\ 10\n
    \s+CallArg\ \(positional\)\n
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

def test_class_basic_e2e!
  assert!(File.exist?(CLASS_BASIC), "class_basic case not found: #{CLASS_BASIC}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("class_basic.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, CLASS_BASIC.to_s)
  assert!(status.success?, "compile class_basic failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "class_basic.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "class_basic.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "30\n", "expected \"30\\n\", got #{normalized.inspect}")

  puts "  OK class_basic e2e"
end

def test_interface_e2e!
  assert!(File.exist?(INTERFACE_TEST), "interface_test case not found: #{INTERFACE_TEST}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("interface_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, INTERFACE_TEST.to_s)
  assert!(status.success?, "compile interface_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "interface_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "interface_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\n", "expected \"42\\n\", got #{normalized.inspect}")

  puts "  OK interface_test e2e"
end

def test_string_e2e!
  assert!(File.exist?(STRING_TEST), "string_test case not found: #{STRING_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("string_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, STDLIB_STRING.to_s, STRING_TEST.to_s)
  assert!(status.success?, "compile string_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "string_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "string_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "Hello L!", "expected \"Hello L!\", got #{normalized.inspect}")

  puts "  OK string_test e2e"
end

def test_generic_e2e!
  assert!(File.exist?(GENERIC_TEST), "generic_test case not found: #{GENERIC_TEST}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", GENERIC_TEST.to_s)
  assert!(ir_st.success?, "emit-llvm generic_test failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("print_val_Point"), "LLVM IR missing specialized print_val_Point\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("generic_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, GENERIC_TEST.to_s)
  assert!(status.success?, "compile generic_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "generic_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "generic_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "88\n", "expected \"88\\n\", got #{normalized.inspect}")

  puts "  OK generic_test e2e"
end

def test_heap_defer_e2e!
  assert!(File.exist?(HEAP_DEFER), "heap_defer case not found: #{HEAP_DEFER}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", HEAP_DEFER.to_s)
  assert!(ir_st.success?, "emit-llvm heap_defer failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("l_alloc"), "LLVM IR missing l_alloc\n#{ir}")
  assert!(ir.include?("l_free"), "LLVM IR missing l_free\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("heap_defer.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, HEAP_DEFER.to_s)
  assert!(status.success?, "compile heap_defer failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "heap_defer.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "heap_defer.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "77\n", "expected \"77\\n\", got #{normalized.inspect}")

  puts "  OK heap_defer e2e"
end

def test_sync_race!
  assert!(File.exist?(SYNC_FAIL), "sync_fail case not found: #{SYNC_FAIL}")
  assert!(File.exist?(SYNC_PASS), "sync_pass case not found: #{SYNC_PASS}")

  fail_out, fail_err, fail_st = run_compiler("--emit-llvm", SYNC_FAIL.to_s)
  assert!(!fail_st.success?, "sync_fail.l should fail to compile")
  combined = "#{fail_out}\n#{fail_err}"
  assert!(
    combined.include?("Data race detected") && combined.include?("mut_count!"),
    "sync_fail missing data-race diagnostic, got:\n#{combined}"
  )
  puts "  OK sync_fail (data race rejected)"

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("sync_pass.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, SYNC_PASS.to_s)
  assert!(status.success?, "compile sync_pass failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "sync_pass.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "sync_pass.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "1\n", "expected \"1\\n\", got #{normalized.inspect}")

  puts "  OK sync_pass e2e"
end

def test_import_module_e2e!
  assert!(File.exist?(MAIN_IMPORT), "main_import case not found: #{MAIN_IMPORT}")
  assert!(File.exist?(MOD_A), "mod_a case not found: #{MOD_A}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("main_import.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, MAIN_IMPORT.to_s)
  assert!(status.success?, "compile main_import failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "main_import.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "main_import.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "99\n", "expected \"99\\n\", got #{normalized.inspect}")

  puts "  OK main_import e2e"
end

def test_array_e2e!
  assert!(File.exist?(ARRAY_TEST), "array_test case not found: #{ARRAY_TEST}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("array_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, STDLIB_ARRAY.to_s, ARRAY_TEST.to_s)
  assert!(status.success?, "compile array_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "array_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "array_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "22\n", "expected \"22\\n\", got #{normalized.inspect}")

  puts "  OK array_test e2e"
end

def test_for_in_e2e!
  assert!(File.exist?(FOR_IN), "for_in case not found: #{FOR_IN}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("for_in.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, STDLIB_ARRAY.to_s, FOR_IN.to_s)
  assert!(status.success?, "compile for_in failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "for_in.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "for_in.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "30\n", "expected \"30\\n\", got #{normalized.inspect}")

  puts "  OK for_in e2e"
end

def test_generic_inference_e2e!
  assert!(File.exist?(GENERIC_INFERENCE), "generic_inference case not found: #{GENERIC_INFERENCE}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", STDLIB_ARRAY.to_s, GENERIC_INFERENCE.to_s)
  assert!(ir_st.success?, "emit-llvm generic_inference failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("Array_i32") || ir.include?("%Array_i32"), "IR missing Array_i32 specialization\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("generic_inference.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, STDLIB_ARRAY.to_s, GENERIC_INFERENCE.to_s)
  assert!(status.success?, "compile generic_inference failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "generic_inference.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "generic_inference.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "100\n", "expected \"100\\n\", got #{normalized.inspect}")

  puts "  OK generic_inference e2e"
end

def test_optional_e2e!
  assert!(File.exist?(OPTIONAL_TEST), "optional_test case not found: #{OPTIONAL_TEST}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", STDLIB_OPTIONAL.to_s, OPTIONAL_TEST.to_s)
  assert!(ir_st.success?, "emit-llvm optional_test failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(
    ir.match?(/%Optional_i32 = type \{ i1, i32 \}/) ||
      ir.include?("%Optional_i32 = type { i1, i32 }"),
    "IR missing Optional_i32 = { i1, i32 }\n#{ir}"
  )

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("optional_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, STDLIB_OPTIONAL.to_s, OPTIONAL_TEST.to_s)
  assert!(status.success?, "compile optional_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "optional_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "optional_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\n99\n", "expected \"42\\n99\\n\", got #{normalized.inspect}")

  puts "  OK optional_test e2e"
end

def test_constructor_e2e!
  assert!(File.exist?(CONSTRUCTOR), "constructor case not found: #{CONSTRUCTOR}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", CONSTRUCTOR.to_s)
  assert!(ir_st.success?, "emit-llvm constructor failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("User_init") || ir.include?("@User_init"), "IR missing User_init\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("constructor.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, CONSTRUCTOR.to_s)
  assert!(status.success?, "compile constructor failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "constructor.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "constructor.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "15\n", "expected \"15\\n\", got #{normalized.inspect}")

  puts "  OK constructor e2e"
end

def test_string_concat_e2e!
  assert!(File.exist?(STRING_CONCAT), "string_concat case not found: #{STRING_CONCAT}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("string_concat.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, STDLIB_STRING.to_s, STRING_CONCAT.to_s)
  assert!(status.success?, "compile string_concat failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "string_concat.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "string_concat.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  # printSelf does not append '\n'; accept either form.
  assert!(
    normalized == "Hello L!" || normalized == "Hello L!\n",
    "expected \"Hello L!\" (optional trailing newline), got #{normalized.inspect}"
  )

  puts "  OK string_concat e2e"
end

def test_kw_mixed_e2e!
  assert!(File.exist?(KW_MIXED), "kw_mixed case not found: #{KW_MIXED}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("kw_mixed.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, STDLIB_STRING.to_s, KW_MIXED.to_s)
  assert!(status.success?, "compile kw_mixed failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "kw_mixed.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "kw_mixed.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "Alice\n20\nadmin\n", "expected Alice/20/admin lines, got #{normalized.inspect}")

  puts "  OK kw_mixed e2e"
end

def test_kw_bad_parse!
  assert!(File.exist?(KW_BAD), "kw_bad case not found: #{KW_BAD}")

  stdout, stderr, status = run_compiler("--dump-ast", KW_BAD.to_s)
  assert!(!status.success?, "kw_bad was expected to fail parsing, but exited 0\n#{stdout}\n#{stderr}")
  combined = "#{stdout}\n#{stderr}"
  assert!(
    combined.match?(/positional argument cannot follow keyword argument/i),
    "kw_bad missing expected parse error, got:\n#{combined}"
  )

  puts "  OK kw_bad parse rejects positional-after-KW"
end

def test_input_readline_e2e!
  assert!(File.exist?(INPUT_TEST), "input_test case not found: #{INPUT_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_IO), "stdlib io not found: #{STDLIB_IO}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("input_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s, STDLIB_STRING.to_s, STDLIB_IO.to_s, INPUT_TEST.to_s
  )
  assert!(status.success?, "compile input_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "input_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s, stdin_data: "L Language\n")
  assert!(st.success?, "input_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized.include?("Hello, L Language!"),
    "expected greeting containing \"Hello, L Language!\", got #{normalized.inspect}"
  )

  puts "  OK input_test e2e (stdin → read_line)"
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
  test_class_basic_e2e!
  test_interface_e2e!
  test_string_e2e!
  test_generic_e2e!
  test_heap_defer_e2e!
  test_sync_race!
  test_import_module_e2e!
  test_array_e2e!
  test_for_in_e2e!
  test_generic_inference_e2e!
  test_optional_e2e!
  test_constructor_e2e!
  test_string_concat_e2e!
  test_kw_mixed_e2e!
  test_kw_bad_parse!
  test_input_readline_e2e!

  puts color("PASS", GREEN)
  exit 0
end

main
