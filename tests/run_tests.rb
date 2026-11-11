#!/usr/bin/env ruby
# frozen_string_literal: true

# L test runner (Ruby automation only). Phase 1: assert Lexer --dump-tokens on golden.l.

require "open3"
require "pathname"

ROOT = Pathname.new(__dir__).parent.expand_path
GOLDEN = ROOT.join("tests", "cases", "golden.l")
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

def dump_tokens_output
  assert!(File.exist?(COMPILER), "compiler not found: #{COMPILER}")
  assert!(File.exist?(GOLDEN), "golden case not found: #{GOLDEN}")

  stdout, stderr, status = Open3.capture3(COMPILER, "--dump-tokens", GOLDEN.to_s)
  assert!(status.success?, "l.exe exited #{status.exitstatus}\n#{stderr}")
  assert!(!stdout.strip.empty?, "dump-tokens produced empty stdout")
  stdout
end

def first_token_line(stdout)
  line = stdout.each_line.map(&:rstrip).find { |l| !l.empty? }
  assert!(!line.nil?, "no token lines in dump-tokens output")
  line
end

def assert_first_location_2_1!(line)
  # Format: Type "lexeme" @ path:line:column  (path may contain ':')
  m = line.match(/:(\d+):(\d+)\s*\z/)
  assert!(!m.nil?, "cannot parse SourceLocation from first token line: #{line}")
  line_no = m[1].to_i
  col_no = m[2].to_i
  assert!(
    line_no == 2 && col_no == 1,
    "first token SourceLocation expected 2:1, got #{line_no}:#{col_no} (#{line})"
  )
end

def assert_required_tokens!(stdout)
  missing = REQUIRED_TOKEN_TYPES.reject { |t| stdout.match?(/^\s*#{Regexp.escape(t)}\b/) }
  assert!(
    missing.empty?,
    "dump-tokens missing required token types: #{missing.join(', ')}"
  )
end

def main
  stdout = dump_tokens_output
  first = first_token_line(stdout)
  assert_first_location_2_1!(first)
  assert_required_tokens!(stdout)

  puts color("PASS", GREEN)
  puts "  golden dump-tokens OK (first token @ 2:1, #{REQUIRED_TOKEN_TYPES.size} kinds present)"
  exit 0
end

main
