#!/usr/bin/env ruby
# frozen_string_literal: true

# L test runner (Ruby automation only).
#
# Usage:
#   ruby tests/run_tests.rb              # smart incremental (skip unchanged PASS)
#   ruby tests/run_tests.rb --all        # force every test, ignore cache (human)
#   ruby tests/run_tests.rb --filter=kw  # targeted run (AI default)
#   ruby tests/run_tests.rb --all --filter=delegate
#
# Policy: compiles with -O0; fail-fast on first FAIL; AI must use --filter only.

require "digest"
require "fileutils"
require "json"
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
HELLO_WORLD = ROOT.join("tests", "cases", "hello_world.l")
CONTROL_FLOW = ROOT.join("tests", "cases", "control_flow.l")
CLASS_BASIC = ROOT.join("tests", "cases", "class_basic.l")
DELEGATE_TEST = ROOT.join("tests", "cases", "delegate_test.l")
DELEGATE_CONFLICT_TEST = ROOT.join("tests", "cases", "delegate_conflict_test.l")
DELEGATE_CONFLICT_BAD = ROOT.join("tests", "cases", "delegate_conflict_bad.l")
DELEGATE_SAME_TYPE_BAD = ROOT.join("tests", "cases", "delegate_same_type_bad.l")
DELEGATE_SAME_TYPE_PARTIAL_BAD = ROOT.join("tests", "cases", "delegate_same_type_partial_bad.l")
COALESCE_TEST = ROOT.join("tests", "cases", "coalesce_test.l")
COALESCE_NON_OPTIONAL_BAD = ROOT.join("tests", "cases", "coalesce_non_optional_bad.l")
PROPERTIES_TEST = ROOT.join("tests", "cases", "properties_test.l")
STDLIB_PROPERTIES = ROOT.join("stdlib", "properties.l")
HTTP_PARSE_TEST = ROOT.join("tests", "cases", "http_parse_test.l")
BREAK_CONTINUE_TEST = ROOT.join("tests", "cases", "break_continue_test.l")
BREAK_OUTSIDE_LOOP_BAD = ROOT.join("tests", "cases", "break_outside_loop_bad.l")
PLUGIN_FRAMEWORK_TEST = ROOT.join("tests", "cases", "plugin_framework_test.l")
THIS_IN_STATIC_BAD = ROOT.join("tests", "cases", "this_in_static_bad.l")
CHAINED_CALL_TEST = ROOT.join("tests", "cases", "chained_call_test.l")
PLUGIN_FRAMEWORK_EXAMPLE = ROOT.join("examples", "plugin_framework.l")
SYNC_TRY_RELEASE = ROOT.join("tests", "cases", "sync_try_release.l")
DELEGATE_OPTIONAL_TEST = ROOT.join("tests", "cases", "delegate_optional_test.l")
DELEGATE_PRIVATE_BAD = ROOT.join("tests", "cases", "delegate_private_bad.l")
STATIC_METHOD_TEST = ROOT.join("tests", "cases", "static_method_test.l")
STATIC_METHOD_BAD = ROOT.join("tests", "cases", "static_method_bad.l")
INTERFACE_TEST = ROOT.join("tests", "cases", "interface_test.l")
DYNAMIC_DISPATCH = ROOT.join("tests", "cases", "dynamic_dispatch.l")
STRING_TEST = ROOT.join("tests", "cases", "string_test.l")
STDLIB_STRING = ROOT.join("stdlib", "string.l")
PRINT_FORMAT = ROOT.join("tests", "cases", "print_format.l")
GENERIC_TEST = ROOT.join("tests", "cases", "generic_test.l")
GENERIC_CONSTRAINT = ROOT.join("tests", "cases", "generic_constraint.l")
GENERIC_CONSTRAINT_BAD = ROOT.join("tests", "cases", "generic_constraint_bad.l")
HEAP_DEFER = ROOT.join("tests", "cases", "heap_defer.l")
AUTO_FREE = ROOT.join("tests", "cases", "auto_free.l")
SYNC_FAIL = ROOT.join("tests", "cases", "sync_fail.l")
SYNC_PASS = ROOT.join("tests", "cases", "sync_pass.l")
MAIN_IMPORT = ROOT.join("tests", "cases", "main_import.l")
MOD_A = ROOT.join("tests", "cases", "mod_a.l")
ARRAY_TEST = ROOT.join("tests", "cases", "array_test.l")
ARRAY_SUGAR = ROOT.join("tests", "cases", "array_sugar.l")
ARRAY_POLY_TEST = ROOT.join("tests", "cases", "array_poly_test.l")
STDLIB_ARRAY = ROOT.join("stdlib", "array.l")
MATCH_TEST = ROOT.join("tests", "cases", "match_test.l")
MATCH_EXPR_TEST = ROOT.join("tests", "cases", "match_expr_test.l")
IMPLICIT_BOXING_TEST = ROOT.join("tests", "cases", "implicit_boxing_test.l")
COALESCE_BOXING_TEST = ROOT.join("tests", "cases", "coalesce_boxing_test.l")
VARIANT_UNBOX_RETURN_TEST = ROOT.join("tests", "cases", "variant_unbox_return_test.l")
COMMENT_POLLUTION_TEST = ROOT.join("tests", "cases", "comment_pollution_test.l")
OPTIONAL_UNWRAP_CHAIN_TEST = ROOT.join("tests", "cases", "optional_unwrap_chain_test.l")
OPTIONAL_IFACE_CHAIN_TEST = ROOT.join("tests", "cases", "optional_iface_chain.l")
CAST_TEST = ROOT.join("tests", "cases", "cast_test.l")
DOWNCAST_TEST = ROOT.join("tests", "cases", "downcast_test.l")
PARTIAL_TEST = ROOT.join("tests", "cases", "partial_test.l")
PROPERTY_FALLBACK = ROOT.join("tests", "cases", "property_fallback.l")
NESTED_INIT = ROOT.join("tests", "cases", "nested_init.l")
APP_CFG = ROOT.join("tests", "cases", "app_cfg.l")
FOR_IN = ROOT.join("tests", "cases", "for_in.l")
GENERIC_INFERENCE = ROOT.join("tests", "cases", "generic_inference.l")
OPTIONAL_TEST = ROOT.join("tests", "cases", "optional_test.l")
STDLIB_OPTIONAL = ROOT.join("stdlib", "optional.l")
RESULT_TEST = ROOT.join("tests", "cases", "result_test.l")
STDLIB_RESULT = ROOT.join("stdlib", "result.l")
TRY_OPERATOR = ROOT.join("tests", "cases", "try_operator.l")
LIFECYCLE_TEST = ROOT.join("tests", "cases", "lifecycle_test.l")
TO_STRING_TEST = ROOT.join("tests", "cases", "to_string_test.l")
CONSTRUCTOR = ROOT.join("tests", "cases", "constructor.l")
STRING_CONCAT = ROOT.join("tests", "cases", "string_concat.l")
KW_MIXED = ROOT.join("tests", "cases", "kw_mixed.l")
KW_BAD = ROOT.join("tests", "cases", "kw_bad.l")
INPUT_TEST = ROOT.join("tests", "cases", "input_test.l")
STDLIB_IO = ROOT.join("stdlib", "io.l")
STDLIB_MEMORY = ROOT.join("stdlib", "memory.l")
STDLIB_THREAD = ROOT.join("stdlib", "thread.l")
STDLIB_NET = ROOT.join("stdlib", "net.l")
STDLIB_FILE = ROOT.join("stdlib", "file.l")
STDLIB_FS = ROOT.join("stdlib", "fs.l")
STDLIB_ENV = ROOT.join("stdlib", "env.l")
STDLIB_CLI = ROOT.join("stdlib", "cli.l")
STDLIB_TIME = ROOT.join("stdlib", "time.l")
STDLIB_REGEX = ROOT.join("stdlib", "regex.l")
STDLIB_PROCESS = ROOT.join("stdlib", "process.l")
STDLIB_PATH = ROOT.join("stdlib", "path.l")
STDLIB_JSON = ROOT.join("stdlib", "json.l")
STDLIB_HTTP = ROOT.join("stdlib", "http.l")
STDLIB_HTTP_SERVER = ROOT.join("stdlib", "http_server.l")
STDLIB_LOG = ROOT.join("stdlib", "log.l")
STDLIB_CONFIG = ROOT.join("stdlib", "config.l")
STRING_EXT_TEST = ROOT.join("tests", "cases", "string_ext_test.l")
PATH_FS_EXT_TEST = ROOT.join("tests", "cases", "path_fs_ext_test.l")
PROCESS_CAPTURE_TEST = ROOT.join("tests", "cases", "process_capture_test.l")
JSON_TEST = ROOT.join("tests", "cases", "json_test.l")
JOIN_TEST = ROOT.join("tests", "cases", "join_test.l")
LOG_CONFIG_TEST = ROOT.join("tests", "cases", "log_config_test.l")
HTTP_STDLIB_TEST = ROOT.join("tests", "cases", "http_stdlib_test.l")
THREAD_TEST = ROOT.join("tests", "cases", "thread_test.l")
CLI_ARGS_TEST = ROOT.join("tests", "cases", "cli_args_test.l")
CLI_FLAGS_TEST = ROOT.join("tests", "cases", "cli_flags_test.l")
ENV_CLIENT_TEST = ROOT.join("tests", "cases", "env_client_test.l")
HTTP_CLIENT = ROOT.join("examples", "http_client.l")
NET_TEST = ROOT.join("tests", "cases", "net_test.l")
HTTP_SERVER = ROOT.join("examples", "http_server.l")
HTTP_SERVER_TEST = ROOT.join("tests", "cases", "http_server_test.l")
FILE_IO_TEST = ROOT.join("tests", "cases", "file_io_test.l")
FS_TEST = ROOT.join("tests", "cases", "fs_test.l")
TIME_TEST = ROOT.join("tests", "cases", "time_test.l")
TIME_FORMAT_TEST = ROOT.join("tests", "cases", "time_format_test.l")
TIMER_TEST = ROOT.join("tests", "cases", "timer_test.l")
REGEX_TEST = ROOT.join("tests", "cases", "regex_test.l")
REGEX_OVERFLOW_TEST = ROOT.join("tests", "cases", "regex_overflow_test.l")
PROCESS_TEST = ROOT.join("tests", "cases", "process_test.l")
CONCURRENCY_STRESS = ROOT.join("tests", "cases", "concurrency_stress.l")
ATOMIC_STRESS = ROOT.join("tests", "cases", "atomic_stress.l")
PKG_TEST = ROOT.join("tests", "cases", "pkg_test")
PKG_CYCLE = ROOT.join("tests", "cases", "pkg_cycle")
CODE_AS_CONFIG = ROOT.join("tests", "cases", "code_as_config")
FFI_EXPORT = ROOT.join("tests", "cases", "ffi_export.l")
EXTERN_TEST = ROOT.join("tests", "cases", "extern_test.l")
CALLBACK_TEST = ROOT.join("tests", "cases", "callback_test.l")
REF_PASS = ROOT.join("tests", "cases", "ref_pass.l")
REF_BAD = ROOT.join("tests", "cases", "ref_bad.l")
MAP_TEST = ROOT.join("tests", "cases", "map_test.l")
STDLIB_MAP = ROOT.join("stdlib", "map.l")
LCMD_L_CALLBACK = ROOT.join("tests", "cases", "lcmd_l_callback.l")
LCMD_PATH = ROOT.join("tests", "cases", "lcmd_path.l")
UID_TEST = ROOT.join("tests", "cases", "uid_test.l")
UID_BAD = ROOT.join("tests", "cases", "uid_bad.l")
CONTEXT_MACRO_TEST = ROOT.join("tests", "cases", "context_macro_test.l")
COMPTIME_OK_TEST = ROOT.join("tests", "cases", "comptime_ok_test.l")
COMPTIME_FAIL_TEST = ROOT.join("tests", "cases", "comptime_fail_test.l")
DOC_SAMPLE = ROOT.join("tests", "cases", "doc_sample.l")
CLONE_TEST = ROOT.join("tests", "cases", "clone_test.l")
FIELD_SYNTAX_TEST = ROOT.join("tests", "cases", "field_syntax_test.l")
FIELD_SYNTAX_BAD = ROOT.join("tests", "cases", "field_syntax_bad.l")
GLOBAL_TEST = ROOT.join("tests", "cases", "global_test.l")
GLOBAL_CLASS_TEST = ROOT.join("tests", "cases", "global_class_test.l")
LAMBDA_TEST = ROOT.join("tests", "cases", "lambda_test.l")
LAMBDA_CAPTURE = ROOT.join("tests", "cases", "lambda_capture.l")
LAMBDA_STATE = ROOT.join("tests", "cases", "lambda_state.l")
CLOSURE_ESCAPE_GOOD = ROOT.join("tests", "cases", "closure_escape_good.l")
COMPOUND_ASSIGN = ROOT.join("tests", "cases", "compound_assign.l")
INC_DEC = ROOT.join("tests", "cases", "inc_dec.l")
MEMORY_STRESS = ROOT.join("tests", "cases", "memory_stress.l")
MEMORY_LEAK_HARD = ROOT.join("tests", "cases", "memory_leak_hard.l")
MULTI_A = ROOT.join("tests", "cases", "multi_a.l")
MULTI_B = ROOT.join("tests", "cases", "multi_b.l")
MULTI_MAIN = ROOT.join("tests", "cases", "multi_main.l")
ARENA_TEST = ROOT.join("tests", "cases", "arena_test.l")
STRING_INTERP = ROOT.join("tests", "cases", "string_interp.l")
TYPE_PROMOTION = ROOT.join("tests", "cases", "type_promotion.l")
DESTRUCTURE = ROOT.join("tests", "cases", "destructure.l")
VARIANT_TEST = ROOT.join("tests", "cases", "variant_test.l")
VARIANT_EXT_TEST = ROOT.join("tests", "cases", "variant_ext_test.l")
STDLIB_VARIANT = ROOT.join("stdlib", "variant.l")
MINI_LEXER = ROOT.join("examples", "mini_lexer.l")
LEXER_DEMO_TEST = ROOT.join("tests", "cases", "lexer_demo_test.l")
MINI_PARSER = ROOT.join("examples", "mini_parser.l")
MINI_PARSER_TEST = ROOT.join("tests", "cases", "mini_parser_test.l")
CONFIG_PARSER = ROOT.join("examples", "config_parser.l")
CONFIG_TEST = ROOT.join("tests", "cases", "config_test.l")
SORT_TEST = ROOT.join("tests", "cases", "sort_test.l")
NODE_HOOK_OPTIN = ROOT.join("tests", "cases", "node_hook_optin.l")
NODE_HOOK_OPTIN_OFF = ROOT.join("tests", "cases", "node_hook_optin_off.l")
OUT_DIR = ROOT.join("build", "clang-debug", "test_out")
COMPILER = ENV.fetch(
  "L_COMPILER",
  ROOT.join("build", "clang-debug", "compiler", "l.exe").to_s
)
COMPILER_LIB = ROOT.join(
  "build", "clang-debug", "compiler",
  Gem.win_platform? ? "l_compiler.lib" : "libl_compiler.a"
)
RUNTIME_LIB = ROOT.join(
  "build", "clang-debug", "runtime",
  Gem.win_platform? ? "l_runtime.lib" : "libl_runtime.a"
)
TEST_CACHE_PATH = ROOT.join("tests", ".test_cache.json")
RUNNER_SOURCE = Pathname.new(__FILE__).expand_path

# C runtime extern modules required by print / heap / Array / String helpers.
def runtime_l_files
  [STDLIB_MEMORY.to_s, STDLIB_IO.to_s, STDLIB_THREAD.to_s]
end

def with_runtime(*paths)
  files = runtime_l_files + paths.map(&:to_s)
  # String.split -> Array<String>: Array template must be parsed *before* String.
  string_i = files.index { |p| File.basename(p) == "string.l" }
  array_i = files.index { |p| File.basename(p) == "array.l" }
  if string_i
    if array_i.nil?
      files.insert(string_i, STDLIB_ARRAY.to_s)
    elsif array_i > string_i
      arr = files.delete_at(array_i)
      files.insert(string_i, arr)
    end
  end
  # env.l get_env -> Optional<String>: Optional template before env.
  env_i = files.index { |p| File.basename(p) == "env.l" }
  opt_i = files.index { |p| File.basename(p) == "optional.l" }
  if env_i
    if opt_i.nil?
      files.insert(env_i, STDLIB_OPTIONAL.to_s)
    elsif opt_i > env_i
      opt = files.delete_at(opt_i)
      files.insert(env_i, opt)
    end
  end
  # cli.l parse_flags -> Map + get_args: Map, Optional, env before cli.
  cli_i = files.index { |p| File.basename(p) == "cli.l" }
  if cli_i
    env2 = files.index { |p| File.basename(p) == "env.l" }
    if env2.nil?
      files.insert(cli_i, STDLIB_ENV.to_s)
      cli_i = files.index { |p| File.basename(p) == "cli.l" }
    elsif env2 > cli_i
      e = files.delete_at(env2)
      cli_i = files.index { |p| File.basename(p) == "cli.l" }
      files.insert(cli_i, e)
      cli_i = files.index { |p| File.basename(p) == "cli.l" }
    end
    map_i = files.index { |p| File.basename(p) == "map.l" }
    cli_i = files.index { |p| File.basename(p) == "cli.l" }
    if map_i.nil?
      files.insert(cli_i, STDLIB_MAP.to_s)
    elsif map_i > cli_i
      mp = files.delete_at(map_i)
      cli_i = files.index { |p| File.basename(p) == "cli.l" }
      files.insert(cli_i, mp)
    end
    opt2 = files.index { |p| File.basename(p) == "optional.l" }
    cli_i = files.index { |p| File.basename(p) == "cli.l" }
    if opt2.nil?
      files.insert(cli_i, STDLIB_OPTIONAL.to_s)
    elsif opt2 > cli_i
      o = files.delete_at(opt2)
      cli_i = files.index { |p| File.basename(p) == "cli.l" }
      files.insert(cli_i, o)
    end
  end
  # fs.l / process.l use Result: Result template before those modules.
  %w[fs.l process.l].each do |base|
    mod_i = files.index { |p| File.basename(p) == base }
    next unless mod_i
    result_i = files.index { |p| File.basename(p) == "result.l" }
    if result_i.nil?
      files.insert(mod_i, STDLIB_RESULT.to_s)
    elsif result_i > mod_i
      res = files.delete_at(result_i)
      # Recompute index after possible delete above when result was after mod.
      mod_i = files.index { |p| File.basename(p) == base }
      files.insert(mod_i, res)
    end
  end
  # time.l format_time -> String: String (and thus Array) before time.
  time_i = files.index { |p| File.basename(p) == "time.l" }
  string_i2 = files.index { |p| File.basename(p) == "string.l" }
  if time_i && string_i2.nil?
    files.insert(time_i, STDLIB_STRING.to_s)
    # Re-run Array-before-String ordering for the newly inserted string.l.
    string_i = files.index { |p| File.basename(p) == "string.l" }
    array_i = files.index { |p| File.basename(p) == "array.l" }
    if string_i
      if array_i.nil?
        files.insert(string_i, STDLIB_ARRAY.to_s)
      elsif array_i > string_i
        arr = files.delete_at(array_i)
        files.insert(string_i, arr)
      end
    end
  end
  files
end

RED = "\e[31m"
GREEN = "\e[32m"
YELLOW = "\e[33m"
RESET = "\e[0m"

class TestFailed < StandardError; end

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
  raise TestFailed, msg
end
def assert!(cond, msg)
  fail_test!(msg) unless cond
end

def run_compiler(*args, stdin_data: nil)
  assert!(File.exist?(COMPILER), "compiler not found: #{COMPILER}")
  args = args.map(&:to_s)
  # Spec 1: E2E compile must pass -O0 (never -O3). Default CLI opt is already 0,
  # but we still inject explicitly. CRITICAL: do NOT put -O0 before a subcommand
  # (`run`/`build`), or argv[1] is no longer the subcommand and `run` is opened
  # as a source file → [ERROR] cannot open module.
  head = args[0].to_s
  has_opt = args.any? { |a| a.match?(/\A-O[0-3]\z/) || a == "--release" }
  skip_force_o0 =
    %w[gen-interface doc repl eval].include?(head) ||
    head.start_with?("--dump-") ||
    has_opt
  unless skip_force_o0
    args =
      if %w[build run].include?(head)
        [head, "-O0", *args.drop(1)]
      else
        ["-O0", *args]
      end
  end
  stdout, stderr, status = Open3.capture3(COMPILER, *args, stdin_data: stdin_data)
  [stdout, stderr, status]
end

# --- Incremental cache / CLI helpers -----------------------------------------
#
# Cache shape:
# {
#   "files": { "rel/path": { "md5": "...", "size": N, "mtime": T }, ... },
#   "tests": { "test_foo!": { "status":"pass", "body_md5":"...", "files":[...keys] } }
# }
# Fast path: size+mtime unchanged → reuse md5 (no file read).
# If size/mtime changed → MD5; same digest still counts as unchanged (touch-only).

def stable_path_key(path)
  p = Pathname(path).expand_path
  begin
    p.relative_path_from(ROOT).to_s.tr("\\", "/")
  rescue ArgumentError
    p.to_s.tr("\\", "/")
  end
end

def path_from_key(key)
  p = Pathname(key)
  p.absolute? ? p : ROOT.join(key)
end

def load_test_cache
  return { "files" => {}, "tests" => {} } unless TEST_CACHE_PATH.exist?
  data = JSON.parse(TEST_CACHE_PATH.read)
  data["files"] = {} unless data["files"].is_a?(Hash)
  data["tests"] = {} unless data["tests"].is_a?(Hash)
  data
rescue StandardError
  { "files" => {}, "tests" => {} }
end

def save_test_cache(cache)
  TEST_CACHE_PATH.write(JSON.pretty_generate(cache))
end

def deep_copy_cache(cache)
  JSON.parse(JSON.generate(cache))
end

# Resolve current content MD5 for path; update cache["files"][key].
# Returns md5 string, or nil if file missing.
def ensure_file_record(path, cache)
  cache["files"] ||= {}
  key = stable_path_key(path)
  p = Pathname(path)
  unless p.file?
    cache["files"].delete(key)
    return nil
  end
  st = p.stat
  size = st.size
  mtime = st.mtime.to_i
  prev = cache["files"][key]
  if prev.is_a?(Hash) &&
     prev["size"] == size &&
     prev["mtime"] == mtime &&
     prev["md5"].is_a?(String) &&
     !prev["md5"].empty?
    return prev["md5"] # fast skip — no MD5 I/O
  end
  md5 = Digest::MD5.file(p.to_s).hexdigest
  cache["files"][key] = { "md5" => md5, "size" => size, "mtime" => mtime }
  md5
end

def runner_method_sources
  @runner_method_sources ||= begin
    map = {}
    # Normalize newlines so Windows CRLF does not churn the method-body digest.
    text = RUNNER_SOURCE.read.gsub("\r\n", "\n")
    text.scan(/^def (test_\w+!?)\n(.*?)(?=^def |\z)/m) do |name, body|
      map[name] = body
    end
    map
  end
end

def under_build_or_out?(path)
  s = Pathname(path).expand_path.to_s
  sep = File::SEPARATOR
  s.include?("#{sep}build#{sep}") || s.include?("test_out") || s.include?("#{sep}Temp#{sep}")
end

def fingerprintable_source?(path)
  path = Pathname(path)
  return false unless path.file?
  return false if under_build_or_out?(path)
  return false if path.expand_path == TEST_CACHE_PATH.expand_path
  # Sources only — never .exe/.o/.obj/.dll/.lib/.json cache artifacts.
  %w[.l .li .toml .c .rb .md].include?(path.extname.downcase)
end

# Alias used by --filter matching (source fixtures only).
def source_input_file?(path)
  fingerprintable_source?(path)
end

def paths_referenced_by_test(name)
  body = runner_method_sources[name.to_s] || ""
  paths = []

  # Precise: ROOT.join("tools", "check_rule_refs.rb") -> one file, not the whole repo.
  body.scan(/ROOT\.join\(([^)]*)\)/) do |(args)|
    parts = args.scan(/"([^"]*)"/).flatten
    next if parts.empty?
    paths << ROOT.join(*parts)
  end

  consts = body.scan(/\b([A-Z][A-Z0-9_]*)\b/).flatten.uniq
  consts.each do |c|
    next if c == "ROOT"
    next unless Object.const_defined?(c, false)
    v = Object.const_get(c)
    next unless v.is_a?(Pathname)
    if v.directory?
      next if under_build_or_out?(v) || v.expand_path == OUT_DIR.expand_path
      Dir.glob(v.join("**", "*").to_s).each do |f|
        paths << Pathname(f) if fingerprintable_source?(f)
      end
    elsif fingerprintable_source?(v)
      paths << v
    end
  end
  # Shared runtime / stdlib .l bindings used by almost every e2e compile.
  runtime_l_files.each { |f| paths << Pathname(f) }
  paths.select { |p| fingerprintable_source?(p) }.uniq
end

def toolchain_paths
  [COMPILER, COMPILER_LIB, RUNTIME_LIB].map { |p| Pathname(p) }.select(&:file?)
end

def test_file_list(name)
  (toolchain_paths + paths_referenced_by_test(name))
    .map { |p| Pathname(p).expand_path }
    .select(&:file?)
    .uniq
    .sort_by { |p| stable_path_key(p) }
end

def test_body_md5(name)
  Digest::MD5.hexdigest(runner_method_sources[name.to_s].to_s)
end

# True when every dependency's content matches the cached file records (fast path).
#
# IMPORTANT: this predicate must be SIDE-EFFECT FREE. An earlier revision called
# `ensure_file_record` here, which rewrote `cache["files"]` on a mismatch; a test
# that ran and passed earlier in the same session (e.g. a broad test that lists
# ROOT as a dependency) would then refresh the shared file records, making later
# tests look "unchanged" and skip wrongly. Compare against the loaded snapshot
# only; `record_test_pass!` is the single writer.
def test_can_skip?(name, entry, cache)
  return false unless entry.is_a?(Hash) && entry["status"] == "pass"
  body = test_body_md5(name)
  return false unless entry["body_md5"] == body
  files = test_file_list(name)
  keys = files.map { |p| stable_path_key(p) }
  return false unless Array(entry["files"]) == keys
  cached_files = cache["files"] || {}
  files.each do |path|
    key = stable_path_key(path)
    expected = cached_files.dig(key, "md5")
    return false if expected.nil? || expected.empty?
    actual = current_md5(path, cached_files[key])
    return false if actual != expected
  end
  true
end

# MD5 of `path` without mutating any cache. Reuses the recorded md5 only when
# size+mtime are unchanged (fast path, no file read).
def current_md5(path, prev_record)
  p = Pathname(path)
  return nil unless p.file?
  st = p.stat
  if prev_record.is_a?(Hash) &&
     prev_record["size"] == st.size &&
     prev_record["mtime"] == st.mtime.to_i &&
     prev_record["md5"].is_a?(String) &&
     !prev_record["md5"].empty?
    return prev_record["md5"]
  end
  Digest::MD5.file(p.to_s).hexdigest
end

def record_test_pass!(name, cache)
  files = test_file_list(name)
  keys = files.map { |p| stable_path_key(p) }
  files.each { |path| ensure_file_record(path, cache) }
  cache["tests"][name.to_s] = {
    "status" => "pass",
    "body_md5" => test_body_md5(name),
    "files" => keys
  }
end

def test_matches_filter?(name, filter)
  return true if filter.nil? || filter.empty?
  key = filter.downcase
  return true if name.to_s.downcase.include?(key)
  paths_referenced_by_test(name).any? do |p|
    fingerprintable_source?(p) && p.basename.to_s.downcase.include?(key)
  end
end

def parse_runner_args!(argv)
  force_all = false
  filter = nil
  argv.each do |a|
    case a
    when "--all"
      force_all = true
    when /\A--filter=(.*)\z/
      filter = Regexp.last_match(1)
    when "--help", "-h"
      puts <<~HELP
        L test runner
          (default)           smart incremental — skip unchanged PASS cases
          --all               force every selected test (ignore cache)
          --filter=keyword    only tests whose name or input path matches keyword
      HELP
      exit 0
    else
      warn color("unknown arg: #{a} (try --help)", RED)
      exit 2
    end
  end
  [force_all, filter]
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

  assert!(
    stdout.include?("define i32 @main(i32 %argc, ptr %argv)"),
    "IR missing define i32 @main(i32 %argc, ptr %argv)\n#{stdout}"
  )
  alloca_count = stdout.scan(/alloca i32/).size
  assert!(alloca_count >= 2, "expected >= 2 alloca i32 (x,y), got #{alloca_count}\n#{stdout}")
  assert!(stdout.match?(/store i32 10/), "IR missing store i32 10\n#{stdout}")
  assert!(stdout.match?(/store i32 20/), "IR missing store i32 20\n#{stdout}")
  assert!(stdout.match?(/\badd i32\b/), "IR missing add i32\n#{stdout}")
  assert!(stdout.match?(/\bret i32\b/), "IR missing ret i32\n#{stdout}")

  puts "  OK codegen_simple --emit-llvm"
end

def test_rule_refs_consistent!
  rules = ROOT.join(".cursor", "rules", "language-rules.mdc")
  script = ROOT.join("tools", "check_rule_refs.rb")
  assert!(File.exist?(rules), "rules file not found: #{rules}")
  assert!(File.exist?(script), "rule-reference guard not found: #{script}")

  stdout, stderr, status = Open3.capture3("ruby", script.to_s)
  combined = "#{stdout}\n#{stderr}".gsub("\r\n", "\n").strip
  assert!(
    status.success?,
    "rule references inconsistent (renumbered sections or stale anchors?)\n#{combined}"
  )
  assert!(
    combined.include?("all references resolve"),
    "unexpected guard output:\n#{combined}"
  )

  puts "  OK rule_refs (#{combined.sub(/\AOK:\s*/, '')})"
end

def test_run_hello_e2e!
  assert!(File.exist?(RUN_HELLO), "run_hello case not found: #{RUN_HELLO}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("hello.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(RUN_HELLO))
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

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(CONTROL_FLOW))
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

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(CLASS_BASIC))
  assert!(status.success?, "compile class_basic failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "class_basic.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "class_basic.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "30\n", "expected \"30\\n\", got #{normalized.inspect}")

  puts "  OK class_basic e2e"
end

def test_delegate_e2e!
  assert!(File.exist?(DELEGATE_TEST), "delegate_test case not found: #{DELEGATE_TEST}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("delegate_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(DELEGATE_TEST))
  assert!(status.success?, "compile delegate_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "delegate_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "delegate_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "Vroom!\n", "expected \"Vroom!\\n\", got #{normalized.inspect}")

  puts "  OK delegate e2e (c.start → Engine.start via delegate GEP this)"
end

def test_delegate_conflict_e2e!
  assert!(File.exist?(DELEGATE_CONFLICT_TEST),
          "delegate_conflict_test not found: #{DELEGATE_CONFLICT_TEST}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("delegate_conflict_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(DELEGATE_CONFLICT_TEST))
  assert!(status.success?,
          "compile delegate_conflict_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "delegate_conflict_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "delegate_conflict_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "engine\nmotor\n", "expected engine/motor lines, got #{normalized.inspect}")

  puts "  OK delegate conflict e2e (explicit c.Engine.start / c.Motor.start)"
end

def test_delegate_conflict_bad_rejected!
  assert!(File.exist?(DELEGATE_CONFLICT_BAD),
          "delegate_conflict_bad not found: #{DELEGATE_CONFLICT_BAD}")
  stdout, stderr, status = run_compiler("--emit-llvm", *with_runtime(DELEGATE_CONFLICT_BAD))
  assert!(!status.success?, "delegate_conflict_bad.l should fail to compile")
  combined = "#{stdout}\n#{stderr}"
  assert!(
    combined.include?("Ambiguous method 'start'") &&
      combined.include?("Engine") &&
      combined.include?("Motor"),
    "expected Ambiguous method diagnostic, got:\n#{combined}"
  )
  puts "  OK delegate_conflict_bad rejected (Ambiguous method 'start')"
end

def test_delegate_same_type_bad_rejected!
  assert!(File.exist?(DELEGATE_SAME_TYPE_BAD),
          "delegate_same_type_bad not found: #{DELEGATE_SAME_TYPE_BAD}")
  stdout, stderr, status = run_compiler("--emit-llvm", *with_runtime(DELEGATE_SAME_TYPE_BAD))
  assert!(!status.success?, "delegate_same_type_bad.l should fail to compile")
  combined = "#{stdout}\n#{stderr}"
  assert!(
    combined.include?(
      "Multiple delegates of the same type 'Engine' are forbidden. " \
      "Use a named field instead."
    ),
    "expected same-type delegate diagnostic, got:\n#{combined}"
  )
  assert!(
    combined.match?(/:\d+:\d+ - Multiple delegates/),
    "expected [file:line:col] prefix in diagnostic, got:\n#{combined}"
  )
  puts "  OK delegate_same_type_bad rejected (duplicate delegate type)"
end

def test_delegate_same_type_partial_bad_rejected!
  assert!(File.exist?(DELEGATE_SAME_TYPE_PARTIAL_BAD),
          "delegate_same_type_partial_bad not found: #{DELEGATE_SAME_TYPE_PARTIAL_BAD}")
  stdout, stderr, status =
    run_compiler("--emit-llvm", *with_runtime(DELEGATE_SAME_TYPE_PARTIAL_BAD))
  assert!(!status.success?, "delegate_same_type_partial_bad.l should fail to compile")
  combined = "#{stdout}\n#{stderr}"
  assert!(
    combined.include?(
      "Multiple delegates of the same type 'Engine' are forbidden. " \
      "Use a named field instead."
    ),
    "expected same-type delegate diagnostic after partial merge, got:\n#{combined}"
  )
  puts "  OK delegate_same_type_partial_bad rejected (duplicate across partial parts)"
end

def test_static_method_e2e!
  assert!(File.exist?(STATIC_METHOD_TEST), "static_method_test not found: #{STATIC_METHOD_TEST}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("static_method_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STATIC_METHOD_TEST))
  assert!(status.success?, "compile static_method_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "static_method_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "static_method_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "30\n", "expected \"30\\n\", got #{normalized.inspect}")

  puts "  OK static_method e2e (MathUtil.add → 30, no this)"
end

def test_static_method_bad_rejected!
  assert!(File.exist?(STATIC_METHOD_BAD), "static_method_bad not found: #{STATIC_METHOD_BAD}")
  stdout, stderr, status = run_compiler("--emit-llvm", STATIC_METHOD_BAD.to_s)
  assert!(!status.success?, "static_method_bad.l should fail to compile")
  combined = "#{stdout}\n#{stderr}"
  assert!(
    combined.match?(/\[ERROR\].*static method/i) ||
      combined.match?(/\[ERROR\].*instance field/i),
    "expected static-field diagnostic, got:\n#{combined}"
  )
  puts "  OK static_method_bad rejected (instance field from static)"
end

def test_interface_e2e!
  assert!(File.exist?(INTERFACE_TEST), "interface_test case not found: #{INTERFACE_TEST}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("interface_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(INTERFACE_TEST))
  assert!(status.success?, "compile interface_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "interface_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "interface_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\n", "expected \"42\\n\", got #{normalized.inspect}")

  puts "  OK interface_test e2e"
end

def test_dynamic_dispatch_e2e!
  assert!(File.exist?(DYNAMIC_DISPATCH), "dynamic_dispatch case not found: #{DYNAMIC_DISPATCH}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(DYNAMIC_DISPATCH))
  assert!(ir_st.success?, "emit-llvm dynamic_dispatch failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  body = ir[/define\s+[^{\n]*@\w*print_any_reader\b[^{]*\{.*?\n\}/m]
  assert!(!body.nil?, "could not find @print_any_reader in IR\n#{ir}")
  assert!(!body.match?(/call\s+i32\s+@Point_read_value\b/),
          "print_any_reader must not statically call Point_read_value:\n#{body}")
  assert!(!body.match?(/call\s+i32\s+@User_read_value\b/),
          "print_any_reader must not statically call User_read_value:\n#{body}")
  assert!(
    body.match?(/call\s+i32\s+%[\w.]+\s*\(/) || body.match?(/call\s+i32\s+%\d+\s*\(/),
    "print_any_reader must call through a function pointer:\n#{body}"
  )

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("dynamic_dispatch.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(DYNAMIC_DISPATCH))
  assert!(status.success?, "compile dynamic_dispatch failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "dynamic_dispatch.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "dynamic_dispatch.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\n99\n", "expected \"42\\n99\\n\", got #{normalized.inspect}")

  puts "  OK dynamic_dispatch e2e (vtable call → 42 / 99)"
end

def test_gen_interface_string!
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  li = OUT_DIR.join("string.li")
  FileUtils.rm_f(li)

  stdout, stderr, status = run_compiler("gen-interface", STDLIB_STRING.to_s, "-o", li.to_s)
  assert!(status.success?, "gen-interface string.l failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(li), "string.li was not produced")

  text = File.read(li)
  assert!(text.include?("pub class String"), "expected pub class String in .li, got:\n#{text}")
  assert!(
    text.include?("pub fun concat(String other) -> String;"),
    "expected concat signature in .li, got:\n#{text}"
  )
  assert!(
    !text.include?("l_str_concat"),
    "interface must not contain l_str_concat implementation, got:\n#{text}"
  )
  assert!(!text.include?("return "), "interface must not contain function bodies, got:\n#{text}")
  assert!(!text.include?("ptr data"), "private field data must be stripped, got:\n#{text}")

  puts "  OK gen-interface string.l → string.li (pub signatures only)"
end

def test_l_doc_markdown!
  assert!(File.exist?(DOC_SAMPLE), "doc_sample case not found: #{DOC_SAMPLE}")

  FileUtils.mkdir_p(OUT_DIR)
  md = OUT_DIR.join("doc_sample.md")
  FileUtils.rm_f(md)

  stdout, stderr, status = run_compiler("doc", DOC_SAMPLE.to_s, "-o", md.to_s)
  assert!(status.success?, "l doc failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(md), "doc_sample.md was not produced")

  text = File.read(md)
  assert!(text.include?("### `pub fun add(i32 a, i32 b) -> i32`"), "missing add heading:\n#{text}")
  assert!(text.include?("Adds two integers."), "missing # doc for add:\n#{text}")
  assert!(text.include?("### `pub interface IReader`"), "missing interface:\n#{text}")
  assert!(text.include?("Readable value source."), "missing // doc for interface:\n#{text}")
  assert!(text.include?("### `pub class Point : IReader`"), "missing class:\n#{text}")
  assert!(text.include?("A 2D point that implements IReader."), "missing # doc for class:\n#{text}")
  assert!(text.include?("### `pub fun read_value() -> i32`"), "missing method:\n#{text}")

  puts "  OK l doc → Markdown (pub fun/class/interface + # // docs)"
end

def test_string_e2e!
  assert!(File.exist?(STRING_TEST), "string_test case not found: #{STRING_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("string_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, STRING_TEST))
  assert!(status.success?, "compile string_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "string_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "string_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "Hello L!", "expected \"Hello L!\", got #{normalized.inspect}")

  puts "  OK string_test e2e"
end

def test_print_format_e2e!
  assert!(File.exist?(PRINT_FORMAT), "print_format case not found: #{PRINT_FORMAT}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("print_format.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, PRINT_FORMAT))
  assert!(status.success?, "compile print_format failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "print_format.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "print_format.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  lines = normalized.split("\n", -1)
  # Trailing newline → last empty entry; require exact four printed lines.
  lines.pop while lines.last == ""
  assert!(lines.size == 4, "expected 4 output lines, got #{lines.inspect}")
  assert!(lines[0] == "9223372036854775807", "i64 line: #{lines[0].inspect}")
  assert!(
    lines[1] == "3.14159" || lines[1] == "3.141590",
    "f64 line expected 3.14159(0), got #{lines[1].inspect}"
  )
  assert!(lines[2] == "true", "bool line: #{lines[2].inspect}")
  assert!(lines[3] == "L-Lang", "String line: #{lines[3].inspect}")

  puts "  OK print_format e2e (i64 / f64 / bool / String)"
end

def test_generic_e2e!
  assert!(File.exist?(GENERIC_TEST), "generic_test case not found: #{GENERIC_TEST}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(GENERIC_TEST))
  assert!(ir_st.success?, "emit-llvm generic_test failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("print_val_Point"), "LLVM IR missing specialized print_val_Point\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("generic_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(GENERIC_TEST))
  assert!(status.success?, "compile generic_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "generic_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "generic_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "88\n", "expected \"88\\n\", got #{normalized.inspect}")

  puts "  OK generic_test e2e"
end

def test_generic_constraint_e2e!
  assert!(File.exist?(GENERIC_CONSTRAINT), "generic_constraint case not found: #{GENERIC_CONSTRAINT}")
  assert!(File.exist?(GENERIC_CONSTRAINT_BAD), "generic_constraint_bad case not found: #{GENERIC_CONSTRAINT_BAD}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("generic_constraint.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(GENERIC_CONSTRAINT))
  assert!(status.success?, "compile generic_constraint failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "generic_constraint.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "generic_constraint.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "Good\n", "expected \"Good\\n\", got #{normalized.inspect}")

  bad_out, bad_err, bad_st = run_compiler("-o", OUT_DIR.join("generic_constraint_bad.exe").to_s,
                                          *with_runtime(GENERIC_CONSTRAINT_BAD))
  assert!(!bad_st.success?, "generic_constraint_bad.l should fail to compile")
  combined = "#{bad_err}\n#{bad_out}"
  assert!(
    combined.match?(/\[ERROR\] .+generic_constraint_bad\.l:\d+:\d+ - Type 'Bad' does not implement interface 'IPrintable'/),
    "expected Type 'Bad' does not implement interface 'IPrintable', got:\n#{combined}"
  )

  puts "  OK generic_constraint e2e (Good ok; Bad rejected)"
end

def test_heap_defer_e2e!
  assert!(File.exist?(HEAP_DEFER), "heap_defer case not found: #{HEAP_DEFER}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(HEAP_DEFER))
  assert!(ir_st.success?, "emit-llvm heap_defer failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("l_alloc"), "LLVM IR missing l_alloc\n#{ir}")
  assert!(ir.include?("l_free"), "LLVM IR missing l_free\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("heap_defer.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(HEAP_DEFER))
  assert!(status.success?, "compile heap_defer failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "heap_defer.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "heap_defer.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "77\n", "expected \"77\\n\", got #{normalized.inspect}")

  puts "  OK heap_defer e2e"
end

def test_auto_free_e2e!
  assert!(File.exist?(AUTO_FREE), "auto_free case not found: #{AUTO_FREE}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(AUTO_FREE))
  assert!(ir_st.success?, "emit-llvm auto_free failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  # Auto-injected l_free must appear in create_and_leak before ret (no explicit defer).
  # File-as-module may mangle to @auto_free_create_and_leak.
  assert!(
    ir.match?(/define\s+[^{]*@\w*create_and_leak[\s\S]*?call\s+.*@l_free[\s\S]*?ret\b/m),
    "LLVM IR missing auto l_free before ret in create_and_leak\n#{ir}"
  )

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("auto_free.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(AUTO_FREE))
  assert!(status.success?, "compile auto_free failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "auto_free.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "auto_free.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\nDone\n", "expected \"42\\nDone\\n\", got #{normalized.inspect}")

  puts "  OK auto_free e2e (heap local → injected l_free)"
end

def test_sync_race!
  assert!(File.exist?(SYNC_FAIL), "sync_fail case not found: #{SYNC_FAIL}")
  assert!(File.exist?(SYNC_PASS), "sync_pass case not found: #{SYNC_PASS}")

  fail_out, fail_err, fail_st = run_compiler("--emit-llvm", SYNC_FAIL.to_s)
  assert!(!fail_st.success?, "sync_fail.l should fail to compile")
  combined = "#{fail_out}\n#{fail_err}"
  assert!(
    combined.match?(/\[ERROR\] .+sync_fail\.l:\d+:\d+ - .*Data race detected/m) &&
      combined.include?("mut_count!"),
    "sync_fail missing located [ERROR] data-race diagnostic, got:\n#{combined}"
  )
  puts "  OK sync_fail (data race rejected)"

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("sync_pass.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(SYNC_PASS))
  assert!(status.success?, "compile sync_pass failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "sync_pass.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "sync_pass.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "1\n", "expected \"1\\n\", got #{normalized.inspect}")

  puts "  OK sync_pass e2e"
end

def test_thread_e2e!
  assert!(File.exist?(THREAD_TEST), "thread_test case not found: #{THREAD_TEST}")
  assert!(File.exist?(STDLIB_THREAD), "stdlib thread not found: #{STDLIB_THREAD}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("thread_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(THREAD_TEST))
  assert!(status.success?, "compile thread_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "thread_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "thread_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "1\n", "expected \"1\\n\", got #{normalized.inspect}")

  puts "  OK thread_test e2e (spawn + sync mutex → OS thread)"
end

def test_concurrency_stress_e2e!
  assert!(File.exist?(CONCURRENCY_STRESS), "concurrency_stress case not found: #{CONCURRENCY_STRESS}")
  assert!(File.exist?(STDLIB_THREAD), "stdlib thread not found: #{STDLIB_THREAD}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("concurrency_stress.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(CONCURRENCY_STRESS))
  assert!(status.success?, "compile concurrency_stress failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "concurrency_stress.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "concurrency_stress.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "100000\n100000\n",
    "expected counter+lock_count \"100000\\n100000\\n\", got #{normalized.inspect}"
  )

  puts "  OK concurrency_stress e2e (10×10000 sync; lock_count==100000)"
end

def test_net_e2e!
  assert!(File.exist?(NET_TEST), "net_test case not found: #{NET_TEST}")
  assert!(File.exist?(STDLIB_NET), "stdlib net not found: #{STDLIB_NET}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_THREAD), "stdlib thread not found: #{STDLIB_THREAD}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("net_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_NET, NET_TEST)
  )
  assert!(status.success?, "compile net_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "net_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "net_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "Hello Server\nHello Client\n",
    "expected \"Hello Server\\nHello Client\\n\", got #{normalized.inspect}"
  )

  puts "  OK net_test e2e (TCP spawn server/client on :38473)"
end

def test_http_server_e2e!
  assert!(File.exist?(HTTP_SERVER), "http_server not found: #{HTTP_SERVER}")
  assert!(File.exist?(HTTP_SERVER_TEST), "http_server_test not found: #{HTTP_SERVER_TEST}")
  assert!(File.exist?(STDLIB_NET), "stdlib net not found: #{STDLIB_NET}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_MAP), "stdlib map not found: #{STDLIB_MAP}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("http_server_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(
      STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_NET,
      HTTP_SERVER, HTTP_SERVER_TEST
    )
  )
  assert!(status.success?, "compile http_server_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "http_server_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "http_server_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized.include?("200 OK"), "expected 200 OK in response, got #{normalized.inspect}")
  assert!(
    normalized.include?("Hello from L HTTP Server!"),
    "expected body in response, got #{normalized.inspect}"
  )
  assert!(normalized.include?("Content-Length:"), "expected Content-Length header, got #{normalized.inspect}")

  puts "  OK http_server e2e (spawn serve_once :8088 + GET / → 200 + body)"
end

def test_file_io_e2e!
  assert!(File.exist?(FILE_IO_TEST), "file_io_test not found: #{FILE_IO_TEST}")
  assert!(File.exist?(STDLIB_FILE), "stdlib file not found: #{STDLIB_FILE}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("file_io_test.exe")
  FileUtils.rm_f(exe)
  FileUtils.rm_f(OUT_DIR.join("test_file_io.txt"))

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_FILE, FILE_IO_TEST)
  )
  assert!(status.success?, "compile file_io_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "file_io_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s, chdir: OUT_DIR.to_s)
  assert!(st.success?, "file_io_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  written = OUT_DIR.join("test_file_io.txt")
  assert!(File.exist?(written), "test_file_io.txt was not written")
  # Exact on-disk bytes prove Lexer escapes + binary File I/O (incl. CR).
  raw = File.binread(written)
  assert!(
    raw.bytes == [72, 101, 108, 108, 111, 10, 76, 45, 76, 97, 110, 103, 33, 13, 10],
    "unexpected file bytes: #{raw.bytes.inspect}"
  )
  # Console capture may normalize CRLF on Windows; still require visible line breaks.
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized.include?("Hello\n"), "expected Hello + newline in stdout, got #{normalized.inspect}")
  assert!(normalized.include?("L-Lang!"), "expected L-Lang! in stdout, got #{normalized.inspect}")

  puts "  OK file_io e2e (write_all/read_all + string escapes \\n/\\r\\n)"
end

def test_fs_e2e!
  assert!(File.exist?(FS_TEST), "fs_test not found: #{FS_TEST}")
  assert!(File.exist?(STDLIB_FS), "stdlib fs not found: #{STDLIB_FS}")
  assert!(File.exist?(STDLIB_FILE), "stdlib file not found: #{STDLIB_FILE}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_RESULT), "stdlib result not found: #{STDLIB_RESULT}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("fs_test.exe")
  FileUtils.rm_f(exe)
  FileUtils.rm_rf(OUT_DIR.join("test_temp_dir"))

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_RESULT, STDLIB_FILE, STDLIB_FS, FS_TEST)
  )
  assert!(status.success?, "compile fs_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "fs_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s, chdir: OUT_DIR.to_s)
  assert!(st.success?, "fs_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized.include?("before:false"), "expected before:false, got #{normalized.inspect}")
  assert!(normalized.include?("create:ok"), "expected create:ok, got #{normalized.inspect}")
  assert!(normalized.include?("after:true"), "expected after:true, got #{normalized.inspect}")
  assert!(normalized.include?("remove_file:ok"), "expected remove_file:ok, got #{normalized.inspect}")
  assert!(normalized.include?("remove_dir:ok"), "expected remove_dir:ok, got #{normalized.inspect}")
  assert!(normalized.include?("cleanup:false"), "expected cleanup:false, got #{normalized.inspect}")
  assert!(!Dir.exist?(OUT_DIR.join("test_temp_dir")), "test_temp_dir should be removed")

  puts "  OK fs e2e (exists / create_dir / remove_file / remove_dir)"
end

def test_time_e2e!
  assert!(File.exist?(TIME_TEST), "time_test not found: #{TIME_TEST}")
  assert!(File.exist?(STDLIB_TIME), "stdlib time not found: #{STDLIB_TIME}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("time_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_TIME, TIME_TEST)
  )
  assert!(status.success?, "compile time_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "time_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "time_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  lines = normalized.split("\n", -1).reject(&:empty?)
  assert!(lines.length >= 3, "expected start/diff/status lines, got #{normalized.inspect}")
  start_ms = Integer(lines[0])
  diff_ms = Integer(lines[1])
  assert!(start_ms > 1_600_000_000_000, "start ms looks non-Unix: #{start_ms}")
  assert!(diff_ms >= 100 && diff_ms < 500, "elapsed #{diff_ms}ms not in [100,500)")
  assert!(lines[2] == "Sleep works!", "expected Sleep works!, got #{lines[2].inspect}")

  puts "  OK time e2e (time_now_ms + sleep(100) → elapsed in [100,500))"
end

def test_time_format_e2e!
  assert!(File.exist?(TIME_FORMAT_TEST), "time_format_test not found: #{TIME_FORMAT_TEST}")
  assert!(File.exist?(STDLIB_TIME), "stdlib time not found: #{STDLIB_TIME}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("time_format_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_TIME, TIME_FORMAT_TEST)
  )
  assert!(status.success?, "compile time_format_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "time_format_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "time_format_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized.match?(/Current time: \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}/),
    "expected formatted current time, got #{normalized.inspect}"
  )
  # localtime(0): 1969 west of UTC, 1970 at/east of UTC.
  assert!(
    normalized.match?(/Epoch year: (1969|1970)/),
    "expected Epoch year 1969 or 1970, got #{normalized.inspect}"
  )

  puts "  OK time_format e2e (strftime %Y-%m-%d %H:%M:%S + epoch year)"
end

def test_timer_e2e!
  assert!(File.exist?(TIMER_TEST), "timer_test not found: #{TIMER_TEST}")
  assert!(File.exist?(STDLIB_TIME), "stdlib time not found: #{STDLIB_TIME}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("timer_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_TIME, TIMER_TEST)
  )
  assert!(status.success?, "compile timer_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "timer_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "timer_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  lines = normalized.split("\n", -1).reject(&:empty?)
  assert!(lines.length >= 4, "expected ≥4 lines, got #{normalized.inspect}")
  assert!(lines[0] == "Start", "expected Start first, got #{lines[0].inspect}")
  assert!(lines[1] == "End of main", "expected End of main second, got #{lines[1].inspect}")
  m = lines[2].match(/\ATimer fired after (\d+) ms\z/)
  assert!(!m.nil?, "expected Timer fired after N ms, got #{lines[2].inspect}")
  elapsed = Integer(m[1])
  assert!(elapsed >= 100 && elapsed < 500, "elapsed #{elapsed}ms not in [100,500)")
  assert!(lines[3] == "Timer callback executed!", "expected callback line, got #{lines[3].inspect}")

  puts "  OK timer e2e (delay → Start / End of main / Timer fired / callback)"
end

def test_string_ext_e2e!
  assert!(File.exist?(STRING_EXT_TEST), "string_ext_test not found")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("string_ext_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STRING_EXT_TEST)
  )
  assert!(status.success?, "compile string_ext_test failed\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "string_ext_test exited #{st.exitstatus}\n#{err}\n#{out}")
  n = out.gsub("\r\n", "\n")
  assert!(n.include?("Hello L"), "expected Hello L, got #{n.inspect}")
  assert!(n.include?("a-b-c"), "expected a-b-c, got #{n.inspect}")
  assert!(n.include?("42"), "expected 42, got #{n.inspect}")
  assert!(n.include?("true"), "expected true, got #{n.inspect}")
  puts "  OK string_ext e2e (trim/contains/replace/join/from_*)"
end

def test_path_fs_ext_e2e!
  assert!(File.exist?(PATH_FS_EXT_TEST), "path_fs_ext_test not found")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("path_fs_ext_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(
      STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_RESULT, STDLIB_FILE, STDLIB_PATH, STDLIB_FS,
      PATH_FS_EXT_TEST
    )
  )
  assert!(status.success?, "compile path_fs_ext_test failed\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "path_fs_ext_test exited #{st.exitstatus}\n#{err}\n#{out}")
  n = out.gsub("\r\n", "\n")
  assert!(n.include?("path_fs_ok"), "expected path_fs_ok, got #{n.inspect}")
  puts "  OK path_fs_ext e2e (join/basename/read_dir/copy/rename)"
end

def test_process_capture_e2e!
  assert!(File.exist?(PROCESS_CAPTURE_TEST), "process_capture_test not found")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("process_capture_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_RESULT, STDLIB_PROCESS, PROCESS_CAPTURE_TEST)
  )
  assert!(status.success?, "compile process_capture_test failed\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "process_capture_test exited #{st.exitstatus}\n#{err}\n#{out}")
  n = out.gsub("\r\n", "\n")
  assert!(n.include?("hello_capture"), "expected hello_capture, got #{n.inspect}")
  assert!(n.include?("capture_ok"), "expected capture_ok, got #{n.inspect}")
  puts "  OK process_capture e2e (stdout capture)"
end

def test_json_e2e!
  assert!(File.exist?(JSON_TEST), "json_test not found")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("json_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_RESULT, STDLIB_MAP, STDLIB_JSON, JSON_TEST)
  )
  assert!(status.success?, "compile json_test failed\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "json_test exited #{st.exitstatus}\n#{err}\n#{out}")
  n = out.gsub("\r\n", "\n")
  assert!(n.include?("L"), "expected name L, got #{n.inspect}")
  assert!(n.include?("42"), "expected 42, got #{n.inspect}")
  assert!(n.include?("json_ok"), "expected json_ok, got #{n.inspect}")
  puts "  OK json e2e (parse flat object + stringify)"
end

def test_join_e2e!
  assert!(File.exist?(JOIN_TEST), "join_test not found")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("join_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_TIME, JOIN_TEST)
  )
  assert!(status.success?, "compile join_test failed\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "join_test exited #{st.exitstatus}\n#{err}\n#{out}")
  lines = out.gsub("\r\n", "\n").split("\n", -1).reject(&:empty?)
  assert!(lines == %w[worker joined], "expected worker then joined, got #{lines.inspect}")
  puts "  OK join e2e (spawn handle + join)"
end

def test_log_config_e2e!
  assert!(File.exist?(LOG_CONFIG_TEST), "log_config_test not found")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("log_config_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(
      STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_RESULT, STDLIB_MAP, STDLIB_FILE, STDLIB_PATH,
      STDLIB_FS, STDLIB_TIME, STDLIB_JSON, STDLIB_LOG, STDLIB_CONFIG, LOG_CONFIG_TEST
    )
  )
  assert!(status.success?, "compile log_config_test failed\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "log_config_test exited #{st.exitstatus}\n#{err}\n#{out}")
  n = out.gsub("\r\n", "\n")
  assert!(n.include?("127.0.0.1"), "expected host, got #{n.inspect}")
  assert!(n.include?("[INFO]"), "expected INFO log, got #{n.inspect}")
  assert!(n.include?("log_config_ok"), "expected log_config_ok, got #{n.inspect}")
  puts "  OK log_config e2e (JSON config + structured log)"
end

def test_http_stdlib_e2e!
  assert!(File.exist?(HTTP_STDLIB_TEST), "http_stdlib_test not found")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("http_stdlib_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(
      STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_NET, STDLIB_TIME, STDLIB_HTTP,
      STDLIB_HTTP_SERVER, HTTP_STDLIB_TEST
    )
  )
  assert!(status.success?, "compile http_stdlib_test failed\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "http_stdlib_test exited #{st.exitstatus}\n#{err}\n#{out}")
  n = out.gsub("\r\n", "\n")
  assert!(n.include?("hello_stdlib_http"), "expected body, got #{n.inspect}")
  assert!(n.include?("http_stdlib_ok"), "expected http_stdlib_ok, got #{n.inspect}")
  puts "  OK http_stdlib e2e (serve_once + http_get + join)"
end

def test_regex_e2e!
  assert!(File.exist?(REGEX_TEST), "regex_test not found: #{REGEX_TEST}")
  assert!(File.exist?(STDLIB_REGEX), "stdlib regex not found: #{STDLIB_REGEX}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("regex_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_REGEX, REGEX_TEST)
  )
  assert!(status.success?, "compile regex_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "regex_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "regex_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized.include?("Found example!"), "expected Found example!, got #{normalized.inspect}")
  assert!(
    normalized.include?("Found email pattern!"),
    "expected Found email pattern!, got #{normalized.inspect}"
  )

  puts "  OK regex e2e (literal + [a-z]+@[a-z]+ mini-engine)"
end

def test_regex_overflow_e2e!
  assert!(File.exist?(REGEX_OVERFLOW_TEST), "regex_overflow_test not found: #{REGEX_OVERFLOW_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("regex_overflow_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_REGEX, REGEX_OVERFLOW_TEST)
  )
  assert!(status.success?, "compile regex_overflow_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "regex_overflow_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  # Must abort: a silent "no match" would be a wrong answer, not a failure signal.
  assert!(!st.success?, "overflow must abort, but the process exited #{st.exitstatus}\n#{out}")
  combined = (out + err).gsub("\r\n", "\n")
  assert!(combined.include?("start"),
          "expected the program to run up to the regex call, got #{combined.inspect}")
  assert!(combined.include?("regex: repetition too long"),
          "expected the overflow panic message, got #{combined.inspect}")
  assert!(!combined.include?("silently truncated"),
          "overflow was silently reported as no-match")

  puts "  OK regex_overflow e2e (stack overflow → panic, no silent truncation)"
end

def test_process_e2e!
  assert!(File.exist?(PROCESS_TEST), "process_test not found: #{PROCESS_TEST}")
  assert!(File.exist?(STDLIB_PROCESS), "stdlib process not found: #{STDLIB_PROCESS}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_RESULT), "stdlib result not found: #{STDLIB_RESULT}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("process_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_RESULT, STDLIB_PROCESS, PROCESS_TEST)
  )
  assert!(status.success?, "compile process_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "process_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "process_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized.include?("L-Process-Test"),
    "expected echo output L-Process-Test, got #{normalized.inspect}"
  )
  assert!(
    normalized.include?("Command executed successfully."),
    "expected success message, got #{normalized.inspect}"
  )

  puts "  OK process e2e (system echo → L-Process-Test + Ok)"
end

def test_cli_args_e2e!
  assert!(File.exist?(CLI_ARGS_TEST), "cli_args_test not found: #{CLI_ARGS_TEST}")
  assert!(File.exist?(STDLIB_ENV), "stdlib env not found: #{STDLIB_ENV}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("cli_args_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_ENV, CLI_ARGS_TEST)
  )
  assert!(status.success?, "compile cli_args_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "cli_args_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s, "hello", "world")
  assert!(st.success?, "cli_args_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "Arguments count: 3\n",
    "expected \"Arguments count: 3\\n\", got #{normalized.inspect}"
  )

  puts "  OK cli_args e2e (get_args → argc includes exe + hello world)"
end

def test_cli_flags_e2e!
  assert!(File.exist?(CLI_FLAGS_TEST), "cli_flags_test not found: #{CLI_FLAGS_TEST}")
  assert!(File.exist?(STDLIB_CLI), "stdlib cli not found: #{STDLIB_CLI}")
  assert!(File.exist?(STDLIB_ENV), "stdlib env not found: #{STDLIB_ENV}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_MAP), "stdlib map not found: #{STDLIB_MAP}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("cli_flags_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(
      STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_ENV, STDLIB_CLI, CLI_FLAGS_TEST
    )
  )
  assert!(status.success?, "compile cli_flags_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "cli_flags_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s, "--version", "--name=Tom")
  assert!(st.success?, "cli_flags_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized.include?("L Language v1.0.0"),
    "expected version banner, got #{normalized.inspect}"
  )
  assert!(
    normalized.include?("Hello, Tom"),
    "expected Hello, Tom, got #{normalized.inspect}"
  )

  puts "  OK cli_flags e2e (pure-L parse_flags --version --name=Tom)"
end

def test_env_client_e2e!
  assert!(File.exist?(ENV_CLIENT_TEST), "env_client_test not found: #{ENV_CLIENT_TEST}")
  assert!(File.exist?(HTTP_CLIENT), "http_client not found: #{HTTP_CLIENT}")
  assert!(File.exist?(HTTP_SERVER), "http_server not found: #{HTTP_SERVER}")
  assert!(File.exist?(STDLIB_ENV), "stdlib env not found: #{STDLIB_ENV}")
  assert!(File.exist?(STDLIB_NET), "stdlib net not found: #{STDLIB_NET}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")
  assert!(File.exist?(STDLIB_MAP), "stdlib map not found: #{STDLIB_MAP}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("env_client_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(
      STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_NET, STDLIB_ENV,
      HTTP_SERVER, HTTP_CLIENT, ENV_CLIENT_TEST
    )
  )
  assert!(status.success?, "compile env_client_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "env_client_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "env_client_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized.include?("PATH exists"), "expected PATH exists, got #{normalized.inspect}")
  assert!(normalized.include?("200 OK"), "expected 200 OK in HTTP response, got #{normalized.inspect}")
  assert!(
    normalized.include?("Hello from L HTTP Server!"),
    "expected HTTP body, got #{normalized.inspect}"
  )

  puts "  OK env_client e2e (get_env PATH + http_get → serve_once :8091)"
end

def test_atomic_stress_e2e!
  assert!(File.exist?(ATOMIC_STRESS), "atomic_stress case not found: #{ATOMIC_STRESS}")
  assert!(File.exist?(STDLIB_THREAD), "stdlib thread not found: #{STDLIB_THREAD}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("atomic_stress.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(ATOMIC_STRESS))
  assert!(status.success?, "compile atomic_stress failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "atomic_stress.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "atomic_stress.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "100000\n", "expected \"100000\\n\", got #{normalized.inspect}")

  puts "  OK atomic_stress e2e (10×10000 lock-free → 100000)"
end

def test_ffi_export_shared!
  assert!(File.exist?(FFI_EXPORT), "ffi_export case not found: #{FFI_EXPORT}")
  assert!(File.exist?(STDLIB_MEMORY), "stdlib memory not found: #{STDLIB_MEMORY}")

  FileUtils.mkdir_p(OUT_DIR)
  dll = OUT_DIR.join(Gem.win_platform? ? "libl_ffi.dll" : "libl_ffi.so")
  FileUtils.rm_f(dll)
  FileUtils.rm_f(OUT_DIR.join("libl_ffi.lib")) # MSVC import lib side-effect
  FileUtils.rm_f(OUT_DIR.join("libl_ffi.exp"))

  # memory.l declares l_free for counter_destroy; l_runtime is linked into the DLL.
  stdout, stderr, status = run_compiler(
    "--shared", "-o", dll.to_s, STDLIB_MEMORY.to_s, FFI_EXPORT.to_s
  )
  assert!(status.success?, "compile --shared ffi_export failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(dll), "shared library was not produced: #{dll}")

  require "fiddle"
  lib = Fiddle.dlopen(dll.to_s)

  add = Fiddle::Function.new(
    lib["add_numbers"],
    [Fiddle::TYPE_INT, Fiddle::TYPE_INT],
    Fiddle::TYPE_INT
  )
  got = add.call(10, 20)
  assert!(got == 30, "FFI add_numbers(10, 20) expected 30, got #{got.inspect}")

  create = Fiddle::Function.new(lib["counter_create"], [Fiddle::TYPE_INT], Fiddle::TYPE_VOIDP)
  incr = Fiddle::Function.new(
    lib["counter_increment"],
    [Fiddle::TYPE_VOIDP, Fiddle::TYPE_INT],
    Fiddle::TYPE_VOID
  )
  get = Fiddle::Function.new(lib["counter_get"], [Fiddle::TYPE_VOIDP], Fiddle::TYPE_INT)
  destroy = Fiddle::Function.new(lib["counter_destroy"], [Fiddle::TYPE_VOIDP], Fiddle::TYPE_VOID)

  handle = create.call(10)
  assert!(!handle.null?, "counter_create returned null")
  incr.call(handle, 5)
  val = get.call(handle)
  assert!(val == 15, "FFI counter_get expected 15, got #{val.inspect}")
  destroy.call(handle)

  puts "  OK ffi_export --shared (Fiddle add_numbers + opaque Counter → 15)"
end

def test_import_module_e2e!
  assert!(File.exist?(MAIN_IMPORT), "main_import case not found: #{MAIN_IMPORT}")
  assert!(File.exist?(MOD_A), "mod_a case not found: #{MOD_A}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("main_import.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(MAIN_IMPORT))
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

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_ARRAY, ARRAY_TEST))
  assert!(status.success?, "compile array_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "array_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "array_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "22\n", "expected \"22\\n\", got #{normalized.inspect}")

  puts "  OK array_test e2e"
end

def test_array_sugar_e2e!
  assert!(File.exist?(ARRAY_SUGAR), "array_sugar case not found: #{ARRAY_SUGAR}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("array_sugar.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_ARRAY, ARRAY_SUGAR))
  assert!(status.success?, "compile array_sugar failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "array_sugar.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "array_sugar.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "20\n40\n60\n", "expected \"20\\n40\\n60\\n\", got #{normalized.inspect}")

  puts "  OK array_sugar e2e (literal + trailing each)"
end

def test_array_poly_e2e!
  assert!(File.exist?(ARRAY_POLY_TEST), "array_poly_test case not found: #{ARRAY_POLY_TEST}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("array_poly_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_ARRAY, ARRAY_POLY_TEST))
  assert!(status.success?, "compile array_poly_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "array_poly_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "array_poly_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "141\n", "expected \"141\\n\", got #{normalized.inspect}")

  puts "  OK array_poly_test e2e (Array<IReader> fat-ptr + dynamic dispatch → 141)"
end

def test_match_e2e!
  assert!(File.exist?(MATCH_TEST), "match_test case not found: #{MATCH_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("match_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(MATCH_TEST))
  assert!(status.success?, "compile match_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "match_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "match_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "One\nTwo\nOther\n", "expected \"One\\nTwo\\nOther\\n\", got #{normalized.inspect}")

  puts "  OK match_test e2e (literal match → if-else)"
end

def test_match_expr_e2e!
  assert!(File.exist?(MATCH_EXPR_TEST), "match_expr_test case not found: #{MATCH_EXPR_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("match_expr_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, MATCH_EXPR_TEST))
  assert!(status.success?, "compile match_expr_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "match_expr_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "match_expr_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  expected = "43\n0\n84\nhello\nmissing\n42\n20\n20\none\nmany\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")

  # The merged arms must go through a PHI node, not a stack slot round-trip.
  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, MATCH_EXPR_TEST))
  assert!(ir_st.success?, "emit-llvm match_expr_test failed\n#{ir_err}\n#{ir}")
  assert!(ir.include?("match.val"), "expected PHI-merged match values in IR\n#{ir[0, 2000]}")

  puts "  OK match_expr_test e2e (some/none destructuring, PHI merge)"
end

def test_implicit_boxing_e2e!
  assert!(File.exist?(IMPLICIT_BOXING_TEST),
          "implicit_boxing_test not found: #{IMPLICIT_BOXING_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("implicit_boxing_test.exe")
  FileUtils.rm_f(exe)

  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_VARIANT,
                       STDLIB_PROPERTIES, IMPLICIT_BOXING_TEST)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?,
          "compile implicit_boxing_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "implicit_boxing_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "implicit_boxing_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  expected = "demo\n42\n1.5\ntrue\n7\nvia-local\ntrue\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")

  # The lowering must go through the declared factories, not a raw store.
  ir, ir_err, ir_st = run_compiler("--emit-llvm", *files)
  assert!(ir_st.success?, "emit-llvm implicit_boxing_test failed\n#{ir_err}\n#{ir}")
  %w[from_string from_i32 from_f64 from_bool].each do |factory|
    assert!(ir.include?("call %Variant @#{factory}"),
            "expected implicit #{factory} call in IR\n#{ir[0, 2000]}")
  end

  puts "  OK implicit_boxing_test e2e (String/i32/f64/bool → Variant.from_*)"
end

def test_coalesce_boxing_e2e!
  assert!(File.exist?(COALESCE_BOXING_TEST),
          "coalesce_boxing_test not found: #{COALESCE_BOXING_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("coalesce_boxing_test.exe")
  FileUtils.rm_f(exe)

  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_VARIANT,
                       STDLIB_PROPERTIES, COALESCE_BOXING_TEST)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?,
          "compile coalesce_boxing_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "coalesce_boxing_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "coalesce_boxing_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  expected = "demo\n42\n1.5\ntrue\nunset\n99\n2.5\ntrue\n7\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")

  # The fallback must be boxed through the declared factories.
  ir, ir_err, ir_st = run_compiler("--emit-llvm", *files)
  assert!(ir_st.success?, "emit-llvm coalesce_boxing_test failed\n#{ir_err}\n#{ir}")
  %w[from_string from_i32 from_f64 from_bool].each do |factory|
    assert!(ir.include?("call %Variant @#{factory}"),
            "expected #{factory} boxing for the '??' fallback\n#{ir[0, 2000]}")
  end

  puts "  OK coalesce_boxing_test e2e (Optional<Variant> ?? String/i32/f64/bool)"
end

def test_variant_unbox_return_e2e!
  assert!(File.exist?(VARIANT_UNBOX_RETURN_TEST),
          "variant_unbox_return_test not found: #{VARIANT_UNBOX_RETURN_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("variant_unbox_return_test.exe")
  FileUtils.rm_f(exe)

  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_VARIANT,
                       STDLIB_PROPERTIES, VARIANT_UNBOX_RETURN_TEST)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?,
          "compile variant_unbox_return_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "variant_unbox_return_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "variant_unbox_return_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  expected = "demo\n42\n1.5\ntrue\nfallback\n42\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")

  # Unboxing must go through the Optional factories and a guard branch.
  ir, ir_err, ir_st = run_compiler("--emit-llvm", *files)
  assert!(ir_st.success?, "emit-llvm variant_unbox_return_test failed\n#{ir_err}\n#{ir}")
  %w[as_string as_i32 as_f64 as_bool].each do |m|
    assert!(ir.include?("@Variant_#{m}"),
            "expected contextual unbox via #{m}\n#{ir[0, 2000]}")
  end
  assert!(ir.include?("unbox.bad"), "expected a panic guard for tag mismatch")

  puts "  OK variant_unbox_return_test e2e (return Variant → String/i32/f64/bool)"
end

def test_comment_pollution_e2e!
  assert!(File.exist?(COMMENT_POLLUTION_TEST),
          "comment_pollution_test not found: #{COMMENT_POLLUTION_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("comment_pollution_test.exe")
  FileUtils.rm_f(exe)

  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_VARIANT,
                       STDLIB_PROPERTIES, COMMENT_POLLUTION_TEST)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?,
          "compile comment_pollution_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "comment_pollution_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "comment_pollution_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  expected = "Variant v = 0; return v.as_string()\n42\nshadow\ntext\n"
  assert!(normalized == expected,
          "comments/strings polluted codegen: expected #{expected.inspect}, got #{normalized.inspect}")

  # The local named `Variant` (actually a String) must not trigger Variant unboxing.
  ir, ir_err, ir_st = run_compiler("--emit-llvm", *files)
  assert!(ir_st.success?, "emit-llvm comment_pollution_test failed\n#{ir_err}\n#{ir}")
  shadowed_body = ir[/define [^\n]*Box_shadowed\([^\n]*\n(?:.*?\n)*?\}\n/m] || ""
  assert!(!shadowed_body.empty?, "Box_shadowed not found in IR")
  assert!(!shadowed_body.include?("unbox.") && !shadowed_body.include?("@Variant_as_"),
         "identifier spelled `Variant` wrongly triggered Variant unboxing\n#{shadowed_body}")
  # The genuine Variant source still unboxes through the declared factory.
  assert!(ir.include?("@Variant_as_string"),
          "expected a real Variant unbox via as_string\n#{ir[0, 2000]}")

  puts "  OK comment_pollution_test e2e (comments/strings/LCMD text stay out of AST)"
end

def test_optional_unwrap_chain_e2e!
  assert!(File.exist?(OPTIONAL_UNWRAP_CHAIN_TEST),
          "optional_unwrap_chain_test not found: #{OPTIONAL_UNWRAP_CHAIN_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("optional_unwrap_chain_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status =
    run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, OPTIONAL_UNWRAP_CHAIN_TEST))
  assert!(status.success?,
          "compile optional_unwrap_chain_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "optional_unwrap_chain_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "optional_unwrap_chain_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  # Interface payload: fat pointer unwrap + vtable dispatch; class payload: member call.
  expected = "triangle\n3\ntriangle\n1\n7\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")

  puts "  OK optional_unwrap_chain_test e2e (unwrap().method() on class + interface)"
end

def test_optional_iface_chain_e2e!
  assert!(File.exist?(OPTIONAL_IFACE_CHAIN_TEST),
          "optional_iface_chain not found: #{OPTIONAL_IFACE_CHAIN_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("optional_iface_chain.exe")
  FileUtils.rm_f(exe)

  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_ARRAY, STDLIB_MAP,
                       OPTIONAL_IFACE_CHAIN_TEST)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?,
          "compile optional_iface_chain failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "optional_iface_chain.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "optional_iface_chain.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  expected = "[start] logger\n[start] auth\nlogger\nauth\nlogger\nauth\n" \
             "[start] logger\nauth\n[start] auth\nauth\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")

  # Every chain must dispatch dynamically through the vtable, never statically.
  ir, ir_err, ir_st = run_compiler("--emit-llvm", *files)
  assert!(ir_st.success?, "emit-llvm optional_iface_chain failed\n#{ir_err}\n#{ir}")
  assert!(ir.include?("chained.fnptr"),
          "expected vtable dispatch for chained interface receivers")

  puts "  OK optional_iface_chain e2e (Optional<IPlugin>.unwrap().method() via vtable)"
end

def test_cast_e2e!
  assert!(File.exist?(CAST_TEST), "cast_test case not found: #{CAST_TEST}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("cast_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_OPTIONAL, CAST_TEST))
  assert!(status.success?, "compile cast_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "cast_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "cast_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\nNot a User\n", "expected \"42\\nNot a User\\n\", got #{normalized.inspect}")

  puts "  OK cast_test e2e (iface downcast → Optional)"
end

def test_downcast_e2e!
  assert!(File.exist?(DOWNCAST_TEST), "downcast_test case not found: #{DOWNCAST_TEST}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(STDLIB_OPTIONAL, DOWNCAST_TEST))
  assert!(ir_st.success?, "emit-llvm downcast_test failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.match?(/typeid_/), "IR missing TypeID globals (typeid_*)\n#{ir}")
  assert!(ir.match?(/\bicmp\s+eq\b/), "IR missing icmp eq TypeID check\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("downcast_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_OPTIONAL, DOWNCAST_TEST))
  assert!(status.success?, "compile downcast_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "downcast_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "downcast_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\nsafe\n", "expected \"42\\nsafe\\n\", got #{normalized.inspect}")

  puts "  OK downcast_test e2e (TypeID vtable[0] → Optional)"
end

def test_partial_e2e!
  assert!(File.exist?(PARTIAL_TEST), "partial_test case not found: #{PARTIAL_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("partial_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, PARTIAL_TEST))
  assert!(status.success?, "compile partial_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "partial_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "partial_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "7\nAlice\n", "expected \"7\\nAlice\\n\", got #{normalized.inspect}")

  puts "  OK partial_test e2e (merged partial class)"
end

def test_property_fallback_e2e!
  assert!(File.exist?(PROPERTY_FALLBACK), "property_fallback case not found: #{PROPERTY_FALLBACK}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("property_fallback.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(PROPERTY_FALLBACK))
  assert!(status.success?, "compile property_fallback failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "property_fallback.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "property_fallback.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "11\n99\n", "expected \"11\\n99\\n\", got #{normalized.inspect}")

  puts "  OK property_fallback e2e (get/set or physical field)"
end

def test_field_syntax_e2e!
  assert!(File.exist?(FIELD_SYNTAX_TEST), "field_syntax_test case not found: #{FIELD_SYNTAX_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("field_syntax_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, FIELD_SYNTAX_TEST))
  assert!(status.success?, "compile field_syntax_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "field_syntax_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "field_syntax_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "Accessor called\n20\n99\n",
    "expected \"Accessor called\\n20\\n99\\n\", got #{normalized.inspect}"
  )

  puts "  OK field_syntax_test e2e (accessor before physical field)"
end

def test_field_syntax_trailing_underscore_rejected!
  assert!(File.exist?(FIELD_SYNTAX_BAD), "field_syntax_bad case not found: #{FIELD_SYNTAX_BAD}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("field_syntax_bad.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(FIELD_SYNTAX_BAD))
  assert!(!status.success?, "field_syntax_bad must fail to compile\n#{stderr}\n#{stdout}")
  combined = "#{stdout}\n#{stderr}"
  assert!(
    combined.include?("_") || combined.include?("Invalid") || combined.include?("expected"),
    "expected trailing-_ diagnostic, got #{combined.inspect}"
  )

  puts "  OK field_syntax_bad rejected (trailing _)"
end

def test_nested_init_e2e!
  assert!(File.exist?(NESTED_INIT), "nested_init case not found: #{NESTED_INIT}")
  assert!(File.exist?(APP_CFG), "app_cfg case not found: #{APP_CFG}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("nested_init.exe")
  FileUtils.rm_f(exe)

  # nested_init.l owns types/global app; app_cfg.l is pure relative-init config.
  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, NESTED_INIT))
  assert!(status.success?, "compile nested_init failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "nested_init.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "nested_init.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "115200\nnone\n", "expected \"115200\\nnone\\n\", got #{normalized.inspect}")

  puts "  OK nested_init e2e (pure app_cfg.l relative-init → __l_global_init)"
end

def test_clone_e2e!
  assert!(File.exist?(CLONE_TEST), "clone_test case not found: #{CLONE_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("clone_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(CLONE_TEST))
  assert!(status.success?, "compile clone_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "clone_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "clone_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "10\n99\n", "expected \"10\\n99\\n\", got #{normalized.inspect}")

  puts "  OK clone_test e2e (auto clone → independent copy)"
end

def test_global_e2e!
  assert!(File.exist?(GLOBAL_TEST), "global_test case not found: #{GLOBAL_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("global_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(GLOBAL_TEST))
  assert!(status.success?, "compile global_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "global_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "global_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "10\n10\n42\n", "expected \"10\\n10\\n42\\n\", got #{normalized.inspect}")

  puts "  OK global_test e2e (module globals + const / constd)"
end

def test_global_class_e2e!
  assert!(File.exist?(GLOBAL_CLASS_TEST), "global_class_test case not found: #{GLOBAL_CLASS_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("global_class_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(GLOBAL_CLASS_TEST))
  assert!(status.success?, "compile global_class_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "global_class_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "global_class_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "1\n2\n3\n", "expected \"1\\n2\\n3\\n\", got #{normalized.inspect}")

  puts "  OK global_class_test e2e (App my_app = App() → __l_global_init + property)"
end

def test_lambda_e2e!
  assert!(File.exist?(LAMBDA_TEST), "lambda_test case not found: #{LAMBDA_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("lambda_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(LAMBDA_TEST))
  assert!(status.success?, "compile lambda_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "lambda_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "lambda_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "20\n10\n", "expected \"20\\n10\\n\", got #{normalized.inspect}")

  puts "  OK lambda_test e2e (Phase-1 lambda → function pointer)"
end

def test_lambda_capture_e2e!
  assert!(File.exist?(LAMBDA_CAPTURE), "lambda_capture case not found: #{LAMBDA_CAPTURE}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("lambda_capture.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(LAMBDA_CAPTURE))
  assert!(status.success?, "compile lambda_capture failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "lambda_capture.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "lambda_capture.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "105\n", "expected \"105\\n\", got #{normalized.inspect}")

  puts "  OK lambda_capture e2e (Phase-2 capturing closure)"
end

def test_lambda_state_e2e!
  assert!(File.exist?(LAMBDA_STATE), "lambda_state case not found: #{LAMBDA_STATE}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("lambda_state.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_ARRAY, LAMBDA_STATE))
  assert!(status.success?, "compile lambda_state failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "lambda_state.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "lambda_state.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "6\n", "expected \"6\\n\", got #{normalized.inspect}")

  puts "  OK lambda_state e2e (each + capturing mutable sum)"
end

def test_closure_escape_good_e2e!
  assert!(File.exist?(CLOSURE_ESCAPE_GOOD), "closure_escape_good case not found: #{CLOSURE_ESCAPE_GOOD}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("closure_escape_good.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(CLOSURE_ESCAPE_GOOD))
  assert!(status.success?, "compile closure_escape_good failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "closure_escape_good.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "closure_escape_good.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "20\n", "expected \"20\\n\", got #{normalized.inspect}")

  puts "  OK closure_escape_good e2e (escaped capturing closure → fat ABI + heap promote)"
end

def test_compound_assign_e2e!
  assert!(File.exist?(COMPOUND_ASSIGN), "compound_assign case not found: #{COMPOUND_ASSIGN}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("compound_assign.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(COMPOUND_ASSIGN))
  assert!(status.success?, "compile compound_assign failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "compound_assign.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "compound_assign.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "6\n", "expected \"6\\n\", got #{normalized.inspect}")

  puts "  OK compound_assign e2e (+= -= *= /= desugar)"
end

def test_inc_dec_e2e!
  assert!(File.exist?(INC_DEC), "inc_dec case not found: #{INC_DEC}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("inc_dec.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(INC_DEC))
  assert!(status.success?, "compile inc_dec failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "inc_dec.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "inc_dec.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "13\n5\n7\n7\n", "expected \"13\\n5\\n7\\n7\\n\", got #{normalized.inspect}")

  puts "  OK inc_dec e2e (++/-- + compound assign)"
end

def test_memory_stress_e2e!
  assert!(File.exist?(MEMORY_STRESS), "memory_stress case not found: #{MEMORY_STRESS}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("memory_stress.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, MEMORY_STRESS))
  assert!(status.success?, "compile memory_stress failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "memory_stress.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "memory_stress.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "No memory leaks!\n",
    "expected \"No memory leaks!\\n\", got #{normalized.inspect}"
  )

  puts "  OK memory_stress e2e (auto-free heap String + concat)"
end

def test_memory_leak_hard_e2e!
  assert!(File.exist?(MEMORY_LEAK_HARD), "memory_leak_hard case not found: #{MEMORY_LEAK_HARD}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("memory_leak_hard.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s, *with_runtime(STDLIB_ARRAY, STDLIB_STRING, MEMORY_LEAK_HARD)
  )
  assert!(status.success?, "compile memory_leak_hard failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "memory_leak_hard.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "memory_leak_hard.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized.include?("No memory leaks!\n"),
    "expected \"No memory leaks!\", got #{normalized.inspect}"
  )
  assert!(
    normalized.match?(/Before: 0\nAfter: 0\n/),
    "expected Before/After both 0, got #{normalized.inspect}"
  )

  puts "  OK memory_leak_hard e2e (concat + Array<IReader> + capturing lambda → no leaks)"
end

def test_multi_file_e2e!
  assert!(File.exist?(MULTI_MAIN), "multi_main case not found: #{MULTI_MAIN}")
  assert!(File.exist?(MULTI_A), "multi_a case not found: #{MULTI_A}")
  assert!(File.exist?(MULTI_B), "multi_b case not found: #{MULTI_B}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("test_multi.exe")
  FileUtils.rm_f(exe)

  # Pass all three files: import resolves multi_a/b; CLI re-entry must hit module cache.
  stdout, stderr, status = run_compiler(
    "-o", exe.to_s, *with_runtime(MULTI_MAIN, MULTI_A, MULTI_B)
  )
  assert!(status.success?, "compile multi_file failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "test_multi.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "test_multi.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "A\nB\n", "expected \"A\\nB\\n\", got #{normalized.inspect}")

  # Import-only path (main pulls a/b) must also work.
  exe2 = OUT_DIR.join("test_multi_import_only.exe")
  FileUtils.rm_f(exe2)
  stdout2, stderr2, status2 = run_compiler("-o", exe2.to_s, *with_runtime(MULTI_MAIN))
  assert!(
    status2.success?,
    "compile multi_file (import-only) failed (#{status2.exitstatus})\n#{stderr2}\n#{stdout2}"
  )
  out2, err2, st2 = Open3.capture3(exe2.to_s)
  assert!(st2.success?, "test_multi_import_only.exe exited #{st2.exitstatus}\n#{err2}")
  normalized2 = out2.gsub("\r\n", "\n")
  assert!(normalized2 == "A\nB\n", "import-only expected \"A\\nB\\n\", got #{normalized2.inspect}")

  puts "  OK multi_file e2e (CLI multi-input + import cache → A/B)"
end

def test_arena_e2e!
  assert!(File.exist?(ARENA_TEST), "arena_test case not found: #{ARENA_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("arena_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, ARENA_TEST))
  assert!(status.success?, "compile arena_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "arena_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "arena_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "Hello World\n",
    "expected \"Hello World\\n\", got #{normalized.inspect}"
  )

  puts "  OK arena e2e (bump pool + auto l_arena_free, no double-free)"
end

def test_string_interp_e2e!
  assert!(File.exist?(STRING_INTERP), "string_interp case not found: #{STRING_INTERP}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("string_interp.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, STRING_INTERP))
  assert!(status.success?, "compile string_interp failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "string_interp.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "string_interp.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "Welcome to L Language, version 1!\nThe sum is 30.\n",
    "expected interp output, got #{normalized.inspect}"
  )

  puts "  OK string_interp e2e (\"…{expr}…\" → String + chain)"
end

def test_destructure_e2e!
  assert!(File.exist?(DESTRUCTURE), "destructure case not found: #{DESTRUCTURE}")
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("destructure.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(DESTRUCTURE))
  assert!(status.success?, "compile destructure failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "destructure.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "destructure.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "10\n20\n", "expected \"10\\n20\\n\", got #{normalized.inspect}")

  puts "  OK destructure e2e (class field order)"
end

def test_variant_e2e!
  assert!(File.exist?(VARIANT_TEST), "variant_test case not found: #{VARIANT_TEST}")
  assert!(File.exist?(STDLIB_VARIANT), "stdlib variant not found: #{STDLIB_VARIANT}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("variant_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_VARIANT, VARIANT_TEST)
  )
  assert!(status.success?, "compile variant_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "variant_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "variant_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "v1 is i32\n42\n99\n",
    "expected \"v1 is i32\\n42\\n99\\n\", got #{normalized.inspect}"
  )

  puts "  OK variant_test e2e (tag check + Optional extract)"
end

def test_variant_ext_e2e!
  assert!(File.exist?(VARIANT_EXT_TEST), "variant_ext_test case not found: #{VARIANT_EXT_TEST}")
  assert!(File.exist?(STDLIB_VARIANT), "stdlib variant not found: #{STDLIB_VARIANT}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("variant_ext_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_VARIANT, VARIANT_EXT_TEST)
  )
  assert!(status.success?, "compile variant_ext_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "variant_ext_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "variant_ext_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  lines = normalized.split("\n", -1)
  assert!(lines.length >= 3, "expected >=3 lines, got #{normalized.inspect}")
  assert!(
    lines[0] == "3.14" || lines[0] == "3.140000" || lines[0].start_with?("3.14"),
    "f64 line expected ~3.14, got #{lines[0].inspect}"
  )
  assert!(lines[1] == "true", "bool line expected true, got #{lines[1].inspect}")
  assert!(lines[2] == "v1 is not bool", "mismatch line: #{lines[2].inspect}")

  puts "  OK variant_ext e2e (from_f64 / from_bool + is_/as_ + wrong-tag)"
end

def test_lexer_demo_e2e!
  assert!(File.exist?(MINI_LEXER), "mini_lexer not found: #{MINI_LEXER}")
  assert!(File.exist?(LEXER_DEMO_TEST), "lexer_demo_test not found: #{LEXER_DEMO_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")
  assert!(File.exist?(STDLIB_VARIANT), "stdlib variant not found: #{STDLIB_VARIANT}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("lexer_demo_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(
      STDLIB_STRING, STDLIB_ARRAY, STDLIB_OPTIONAL, STDLIB_VARIANT,
      MINI_LEXER, LEXER_DEMO_TEST
    )
  )
  assert!(status.success?, "compile lexer_demo_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "lexer_demo_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "lexer_demo_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "let\nx\n=\n42\n",
    "expected \"let\\nx\\n=\\n42\\n\", got #{normalized.inspect}"
  )

  puts "  OK lexer_demo e2e (mini_lexer tokenize + Variant print)"
end

def test_mini_parser_e2e!
  assert!(File.exist?(MINI_PARSER), "mini_parser not found: #{MINI_PARSER}")
  assert!(File.exist?(MINI_PARSER_TEST), "mini_parser_test not found: #{MINI_PARSER_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("mini_parser_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(
      STDLIB_STRING, STDLIB_ARRAY, STDLIB_OPTIONAL,
      MINI_PARSER, MINI_PARSER_TEST
    )
  )
  assert!(status.success?, "compile mini_parser_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "mini_parser_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "mini_parser_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  expected = "Type: i32\nName: age\nValue: 18\nType: inferred\nName: name\nValue: L\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")

  puts "  OK mini_parser e2e (VarDecl typed + := inferred)"
end

def test_config_e2e!
  assert!(File.exist?(CONFIG_PARSER), "config_parser not found: #{CONFIG_PARSER}")
  assert!(File.exist?(CONFIG_TEST), "config_test not found: #{CONFIG_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")
  assert!(File.exist?(STDLIB_MAP), "stdlib map not found: #{STDLIB_MAP}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("config_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(
      STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP,
      CONFIG_PARSER, CONFIG_TEST
    )
  )
  assert!(status.success?, "compile config_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "config_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "config_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  expected = "127.0.0.1\n8080\nL-Lang\na=b=c\nstill_stored\nskipped\nskipped\nok\n"
  assert!(
    normalized == expected,
    "expected #{expected.inspect}, got #{normalized.inspect}"
  )

  puts "  OK config_test e2e (robust #/blank/malformed/multi-= → Map<String>)"
end

def test_sort_e2e!
  assert!(File.exist?(SORT_TEST), "sort_test case not found: #{SORT_TEST}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("sort_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_ARRAY, SORT_TEST))
  assert!(status.success?, "compile sort_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "sort_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "sort_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "1\n2\n5\n5\n6\n9\n", "expected sorted 1..9, got #{normalized.inspect}")

  puts "  OK sort_test e2e (bubble_sort Array<i32>)"
end

def test_type_promotion_e2e!
  assert!(File.exist?(TYPE_PROMOTION), "type_promotion case not found: #{TYPE_PROMOTION}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(TYPE_PROMOTION))
  assert!(ir_st.success?, "emit-llvm type_promotion failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(
    ir.match?(/\bsext\b/i) || ir.include?("sext "),
    "LLVM IR missing sext (i32→i64 promotion)\n#{ir}"
  )
  assert!(
    ir.match?(/\bsitofp\b/i) || ir.include?("sitofp "),
    "LLVM IR missing sitofp (int→f64 promotion)\n#{ir}"
  )
  assert!(
    ir.include?("fmul") || ir.include?("fadd"),
    "LLVM IR missing float arithmetic after promotion\n#{ir}"
  )

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("type_promotion.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(TYPE_PROMOTION))
  assert!(status.success?, "compile type_promotion failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "type_promotion.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "type_promotion.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "30\n31.4\n",
    "expected \"30\\n31.4\\n\", got #{normalized.inspect}"
  )

  puts "  OK type_promotion e2e (i32→i64 sext, i32→f64 sitofp)"
end

def test_for_in_e2e!
  assert!(File.exist?(FOR_IN), "for_in case not found: #{FOR_IN}")
  assert!(File.exist?(STDLIB_ARRAY), "stdlib array not found: #{STDLIB_ARRAY}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("for_in.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_ARRAY, FOR_IN))
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

  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(STDLIB_ARRAY, GENERIC_INFERENCE))
  assert!(ir_st.success?, "emit-llvm generic_inference failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("Array_i32") || ir.include?("%Array_i32"), "IR missing Array_i32 specialization\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("generic_inference.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_ARRAY, GENERIC_INFERENCE))
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

  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(STDLIB_OPTIONAL, OPTIONAL_TEST))
  assert!(ir_st.success?, "emit-llvm optional_test failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(
    ir.match?(/%Optional_i32 = type \{ i1, i32 \}/) ||
      ir.include?("%Optional_i32 = type { i1, i32 }"),
    "IR missing Optional_i32 = { i1, i32 }\n#{ir}"
  )

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("optional_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_OPTIONAL, OPTIONAL_TEST))
  assert!(status.success?, "compile optional_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "optional_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "optional_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\n99\n", "expected \"42\\n99\\n\", got #{normalized.inspect}")

  puts "  OK optional_test e2e"
end

def test_coalesce_e2e!
  assert!(File.exist?(COALESCE_TEST), "coalesce_test case not found: #{COALESCE_TEST}")

  ir, ir_err, ir_st =
    run_compiler("--emit-llvm", *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, COALESCE_TEST))
  assert!(ir_st.success?, "emit-llvm coalesce_test failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  # `??` must lower to a real branch with a phi merge, not a masked select.
  assert!(ir.include?("coal.some"), "IR missing coal.some block\n#{ir}")
  assert!(ir.include?("coal.none"), "IR missing coal.none block\n#{ir}")
  assert!(ir.include?("coalesce"), "IR missing coalesce phi\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("coalesce_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status =
    run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, COALESCE_TEST))
  assert!(status.success?, "compile coalesce_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "coalesce_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "coalesce_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "42\n7\nguest\nadmin\n",
    "expected \"42\\n7\\nguest\\nadmin\\n\", got #{normalized.inspect}"
  )

  puts "  OK coalesce e2e (opt ?? fallback, empty → default, some → payload)"
end

def test_coalesce_non_optional_bad!
  assert!(File.exist?(COALESCE_NON_OPTIONAL_BAD), "case not found: #{COALESCE_NON_OPTIONAL_BAD}")

  out, err, st =
    run_compiler("-o", OUT_DIR.join("coalesce_bad.exe").to_s,
                 *with_runtime(STDLIB_OPTIONAL, COALESCE_NON_OPTIONAL_BAD))
  combined = "#{out}\n#{err}"
  assert!(!st.success?, "expected '??' on non-Optional to fail, but it compiled\n#{combined}")
  assert!(
    combined.include?("left operand of '??' must be Optional<T>"),
    "expected Optional<T> diagnostic, got:\n#{combined}"
  )

  puts "  OK coalesce rejects non-Optional left operand"
end

def test_properties_e2e!
  assert!(File.exist?(PROPERTIES_TEST), "properties_test case not found: #{PROPERTIES_TEST}")
  assert!(File.exist?(STDLIB_PROPERTIES), "stdlib properties not found: #{STDLIB_PROPERTIES}")

  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_VARIANT,
                       STDLIB_PROPERTIES, PROPERTIES_TEST)

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("properties_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?, "compile properties_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "properties_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "properties_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  expected = "empty\nadmin\nunknown\nroot\n20\ntwenty\n0\n" \
             "string-not-i32\nstring-as-i32-empty\nabsent-is-empty\n" \
             "3\n0\nfalse\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")

  # The delegate bag must be a live embedded instance, not a null pointer.
  ir, ir_err, ir_st = run_compiler("--emit-llvm", *files)
  assert!(ir_st.success?, "emit-llvm properties_test failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("%CPropertys = type { %Map_Variant }"),
          "IR missing embedded Map<Variant> in CPropertys\n#{ir}")

  puts "  OK properties e2e (set/get via Variant + ?? defaults + 9 edge cases)"
end

# Minimal HTTP response parser: status code + CPropertys headers, pure string
# processing (no sockets). Covers the adversarial edges: empty response, missing
# CRLF, absent header, repeated header, ':' inside a value.
def test_http_parse_e2e!
  assert!(File.exist?(HTTP_PARSE_TEST), "http_parse_test not found: #{HTTP_PARSE_TEST}")

  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_VARIANT,
                       STDLIB_PROPERTIES, HTTP_PARSE_TEST)

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("http_parse_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?, "compile http_parse_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "http_parse_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "http_parse_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  expected = "200\napplication/json\n18\n" \
             "404\n\nL\n" \
             "0\n\n" \
             "500\n" \
             "http://x/y:8080/z\n" \
             "second\n" \
             "padded\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")

  # Headers must live in a real CPropertys (embedded Map<Variant>), not a local sweep.
  ir, ir_err, ir_st = run_compiler("--emit-llvm", *files)
  assert!(ir_st.success?, "emit-llvm http_parse_test failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("%CPropertys = type { %Map_Variant }"),
          "IR missing embedded Map<Variant> in CPropertys\n#{ir}")

  puts "  OK http_parse e2e (status + CPropertys headers, 7 edge cases)"
end

def test_break_continue_e2e!
  assert!(File.exist?(BREAK_CONTINUE_TEST), "break_continue_test not found: #{BREAK_CONTINUE_TEST}")
  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, BREAK_CONTINUE_TEST)
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("break_continue_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?, "compile break_continue_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "break_continue_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  expected = "18\n7\n" \
             "4\n" \
             "0\n1\n3\n4\n" \
             "2\n12\n" \
             "cleanup\n2\n" \
             "-1\n42\n" \
             "-7\n-2.5\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")
  puts "  OK break/continue e2e (while, Array for-in, range for-in, nested, defer, ?? -1)"
end

def test_break_outside_loop_rejected!
  assert!(File.exist?(BREAK_OUTSIDE_LOOP_BAD),
          "break_outside_loop_bad not found: #{BREAK_OUTSIDE_LOOP_BAD}")
  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, BREAK_OUTSIDE_LOOP_BAD)
  stdout, stderr, status = run_compiler("--emit-llvm", *files)
  assert!(!status.success?, "break outside a loop must not compile")
  combined = (stdout || "") + (stderr || "")
  assert!(combined.include?("'break' is only allowed inside a loop"),
          "missing loop-context diagnostic\n#{combined}")
  puts "  OK break outside loop rejected"
end

def test_plugin_framework_e2e!
  assert!(File.exist?(PLUGIN_FRAMEWORK_TEST),
          "plugin_framework_test not found: #{PLUGIN_FRAMEWORK_TEST}")
  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP,
                       STDLIB_VARIANT, STDLIB_PROPERTIES, PLUGIN_FRAMEWORK_TEST)
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("plugin_framework_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?, "compile plugin_framework_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "plugin_framework_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  expected = "0\n" \
             "2\n" \
             "0.1.0\n" \
             "[start] logger ready\n[start] auth ready\n" \
             "logger\nauth\n" \
             "2\n" \
             "[start] logger ready\n" \
             "logger\nauth\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")
  puts "  OK plugin framework e2e (interface array + this + field for-in + chained calls)"
end

def test_chained_call_e2e!
  assert!(File.exist?(CHAINED_CALL_TEST), "chained_call_test not found: #{CHAINED_CALL_TEST}")
  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, CHAINED_CALL_TEST)
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("chained_call_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?, "compile chained_call_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "chained_call_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  expected = "9\n10\n" \
             "square\nrect\n" \
             "touch square\ntouch rect\n" \
             "6\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")
  puts "  OK chained calls on interface elements (i32 / String / void / 3-level)"
end

def test_this_in_static_rejected!
  assert!(File.exist?(THIS_IN_STATIC_BAD), "this_in_static_bad not found: #{THIS_IN_STATIC_BAD}")
  files = with_runtime(THIS_IN_STATIC_BAD)
  stdout, stderr, status = run_compiler("--emit-llvm", *files)
  assert!(!status.success?, "'this' inside a static method must not compile")
  combined = (stdout || "") + (stderr || "")
  assert!(combined.include?("cannot use 'this' inside a static method"),
          "missing static-this diagnostic\n#{combined}")
  puts "  OK 'this' inside static method rejected"
end

def test_delegate_optional_e2e!
  assert!(File.exist?(DELEGATE_OPTIONAL_TEST),
          "delegate_optional_test not found: #{DELEGATE_OPTIONAL_TEST}")
  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, DELEGATE_OPTIONAL_TEST)
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("delegate_optional_test.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?,
          "compile delegate_optional_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "delegate_optional_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  expected = "anonymous\nAlice\nAlice\nAlice\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")
  puts "  OK delegate promotes Optional-returning methods (chain + ??)"
end

def test_delegate_private_bad_rejected!
  assert!(File.exist?(DELEGATE_PRIVATE_BAD),
          "delegate_private_bad not found: #{DELEGATE_PRIVATE_BAD}")
  stdout, stderr, status = run_compiler("--emit-llvm", *with_runtime(DELEGATE_PRIVATE_BAD))
  assert!(!status.success?, "delegate_private_bad should fail to compile\n#{stdout}\n#{stderr}")
  combined = "#{stdout}\n#{stderr}"
  assert!(combined.match?(/\[ERROR\] .+delegate_private_bad\.l:\d+:\d+ - .*unknown property 'token'/m),
          "missing located [ERROR] private-field diagnostic, got:\n#{combined}")
  puts "  OK delegate does not leak private fields"
end

def test_sync_try_release!
  assert!(File.exist?(SYNC_TRY_RELEASE), "sync_try_release not found: #{SYNC_TRY_RELEASE}")
  files = with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_RESULT, SYNC_TRY_RELEASE)
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("sync_try_release.exe")
  FileUtils.rm_f(exe)
  stdout, stderr, status = run_compiler("-o", exe.to_s, *files)
  assert!(status.success?,
          "compile sync_try_release failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "sync_try_release.exe exited #{st.exitstatus} (possible deadlock)\n#{err}\n#{out}")
  lines = out.gsub("\r\n", "\n").split("\n")
  expected = %w[6 boom 2 2 6 6] + ["recovered"]
  assert!(lines == expected, "expected #{expected.inspect}, got #{lines.inspect}")
  # Non-negotiable: every lock acquired inside sync was released on the ? path.
  assert!(lines[2] == lines[3], "single-thread lock/unlock mismatch: #{lines[2]}/#{lines[3]}")
  assert!(lines[4] == lines[5], "multi-thread lock/unlock mismatch: #{lines[4]}/#{lines[5]}")
  puts "  OK `?` inside sync releases the lock (no deadlock, lock==unlock)"
end

def test_result_e2e!
  assert!(File.exist?(RESULT_TEST), "result_test case not found: #{RESULT_TEST}")
  assert!(File.exist?(STDLIB_RESULT), "stdlib result not found: #{STDLIB_RESULT}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  ir, ir_err, ir_st = run_compiler(
    "--emit-llvm",
    *with_runtime(STDLIB_STRING, STDLIB_RESULT, RESULT_TEST)
  )
  assert!(ir_st.success?, "emit-llvm result_test failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(
    ir.include?("Result_i32_String") || ir.include?("%Result_i32_String"),
    "IR missing Result_i32_String specialization\n#{ir}"
  )

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("result_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_RESULT, RESULT_TEST)
  )
  assert!(status.success?, "compile result_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "result_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "result_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "5\nDivision by zero\n",
    "expected \"5\\nDivision by zero\\n\", got #{normalized.inspect}"
  )

  puts "  OK result_test e2e (Result<T,E> Ok / Err)"
end

def test_try_operator_e2e!
  assert!(File.exist?(TRY_OPERATOR), "try_operator case not found: #{TRY_OPERATOR}")
  assert!(File.exist?(STDLIB_RESULT), "stdlib result not found: #{STDLIB_RESULT}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("try_operator.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_RESULT, TRY_OPERATOR)
  )
  assert!(status.success?, "compile try_operator failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "try_operator.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "try_operator.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "10\nDiv by zero\n",
    "expected \"10\\nDiv by zero\\n\", got #{normalized.inspect}"
  )

  puts "  OK try_operator e2e (Result ? early-return)"
end

def test_lifecycle_e2e!
  assert!(File.exist?(LIFECYCLE_TEST), "lifecycle_test case not found: #{LIFECYCLE_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("lifecycle_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(LIFECYCLE_TEST))
  assert!(status.success?, "compile lifecycle_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "lifecycle_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "lifecycle_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  expected = "System Initialized\nMain is running\nSystem Cleaned up\n"
  assert!(
    normalized == expected,
    "expected #{expected.inspect}, got #{normalized.inspect}"
  )

  puts "  OK lifecycle_test e2e (__init / __deinit around main)"
end

def test_to_string_e2e!
  assert!(File.exist?(TO_STRING_TEST), "to_string_test case not found: #{TO_STRING_TEST}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("to_string_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, TO_STRING_TEST)
  )
  assert!(status.success?, "compile to_string_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "to_string_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "to_string_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "Point { x: 10, y: 20 }\n",
    "expected \"Point { x: 10, y: 20 }\\n\", got #{normalized.inspect}"
  )

  puts "  OK to_string_test e2e (auto to_string field dump)"
end

def test_constructor_e2e!
  assert!(File.exist?(CONSTRUCTOR), "constructor case not found: #{CONSTRUCTOR}")

  ir, ir_err, ir_st = run_compiler("--emit-llvm", *with_runtime(CONSTRUCTOR))
  assert!(ir_st.success?, "emit-llvm constructor failed (#{ir_st.exitstatus})\n#{ir_err}\n#{ir}")
  assert!(ir.include?("User_init") || ir.include?("@User_init"), "IR missing User_init\n#{ir}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("constructor.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(CONSTRUCTOR))
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

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, STRING_CONCAT))
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

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(STDLIB_STRING, KW_MIXED))
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
    combined.match?(/\[ERROR\] .+kw_bad\.l:\d+:\d+ - .*positional argument cannot follow keyword argument/im),
    "kw_bad missing located [ERROR] parse diagnostic, got:\n#{combined}"
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
    "-o", exe.to_s, *with_runtime(STDLIB_STRING, INPUT_TEST)
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

def test_extern_strlen_e2e!
  assert!(File.exist?(EXTERN_TEST), "extern_test case not found: #{EXTERN_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("extern_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(EXTERN_TEST))
  assert!(status.success?, "compile extern_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "extern_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "extern_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "5\n", "expected \"5\\n\", got #{normalized.inspect}")

  puts "  OK extern_test e2e (extern module strlen)"
end

def test_callback_e2e!
  assert!(File.exist?(CALLBACK_TEST), "callback_test case not found: #{CALLBACK_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("callback_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(CALLBACK_TEST))
  assert!(status.success?, "compile callback_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "callback_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "callback_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(
    normalized == "Callback triggered with: \n42\n",
    "expected \"Callback triggered with: \\n42\\n\", got #{normalized.inspect}"
  )

  puts "  OK callback_test e2e (L fun → C register → C trigger → L callback)"
end

def test_ref_pass_e2e!
  assert!(File.exist?(REF_PASS), "ref_pass case not found: #{REF_PASS}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("ref_pass.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(REF_PASS))
  assert!(status.success?, "compile ref_pass failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "ref_pass.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "ref_pass.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "10\n99\n", "expected \"10\\n99\\n\", got #{normalized.inspect}")

  puts "  OK ref_pass e2e (class pass-by-ref + mutable !)"
end

def test_node_hook_optin_e2e!
  assert!(File.exist?(NODE_HOOK_OPTIN), "node_hook_optin case not found: #{NODE_HOOK_OPTIN}")
  assert!(File.exist?(NODE_HOOK_OPTIN_OFF), "node_hook_optin_off case not found: #{NODE_HOOK_OPTIN_OFF}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("node_hook_optin.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(NODE_HOOK_OPTIN))
  assert!(status.success?, "compile node_hook_optin failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "node_hook_optin.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "node_hook_optin.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "888\n", "expected \"888\\n\", got #{normalized.inspect}")

  # Disabled-hook class must NOT get interpreter injection.
  off_exe = OUT_DIR.join("node_hook_optin_off.exe")
  FileUtils.rm_f(off_exe)
  off_out, off_err, off_st = run_compiler("-o", off_exe.to_s, *with_runtime(NODE_HOOK_OPTIN_OFF))
  assert!(!off_st.success?, "expected compile failure after disable_class_hook, got success\n#{off_err}\n#{off_out}")
  combined = "#{off_err}\n#{off_out}"
  assert!(
    combined.match?(/leaked_from_disabled_hook|unknown function|undeclared/i),
    "expected error about missing leaked_from_disabled_hook, got:\n#{combined}"
  )

  puts "  OK node_hook_optin e2e (enable/disable class hook)"
end

def test_repl_e2e!
  assert!(File.exist?(COMPILER), "compiler not found: #{COMPILER}")

  # Drive the REPL via stdin: expression print, a syntax error, then recovery + exit.
  script = <<~INPUT
    print(1 + 2)
    print(
    print(7)
    exit
  INPUT

  stdout, stderr, status = Open3.capture3(COMPILER, "repl", stdin_data: script)
  assert!(status.success?, "repl exited #{status.exitstatus}\n#{stderr}\n#{stdout}")

  combined_out = stdout.gsub("\r\n", "\n")
  assert!(combined_out.include?("L> "), "expected L> prompt, got:\n#{combined_out.inspect}")
  # ORC JIT runs in-process; print() shares stdout with the prompt.
  assert!(combined_out.include?("3"), "expected print(1 + 2) → 3, got:\n#{combined_out.inspect}")
  assert!(combined_out.include?("7"), "expected recovery print(7) → 7, got:\n#{combined_out.inspect}")

  combined_err = stderr.gsub("\r\n", "\n")
  assert!(
    combined_err.match?(/\[ERROR\] <repl>:\d+:\d+/),
    "expected [ERROR] <repl>:line:col after bad input, got:\n#{combined_err}"
  )

  puts "  OK l repl e2e (ORC JIT in-memory + error recovery)"
end

def test_eval_jit_e2e!
  assert!(File.exist?(COMPILER), "compiler not found: #{COMPILER}")
  src = "fun main() -> i32 { print(123); return 0; }"
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  stdout, stderr, status = run_compiler("eval", src)
  t1 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  assert!(status.success?, "l eval failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(stdout.gsub("\r\n", "\n") == "123\n", "expected \"123\\n\", got #{stdout.inspect}")
  ms = (t1 - t0) * 1000.0
  assert!(ms < 5000.0, "l eval wall time #{format('%.1f', ms)}ms (expected <5s)")
  puts "  OK l eval ORC JIT e2e (print 123, #{format('%.0f', ms)}ms)"
end

def test_cli_run_e2e!
  assert!(File.exist?(HELLO_WORLD), "hello_world case not found: #{HELLO_WORLD}")
  stdout, stderr, status = run_compiler("run", HELLO_WORLD.to_s)
  assert!(status.success?, "l run hello_world.l failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  normalized = stdout.gsub("\r\n", "\n")
  assert!(normalized == "42\n", "l run expected \"42\\n\", got #{normalized.inspect}")
  puts "  OK l run hello_world.l (compile + execute → 42)"
end

def test_cli_run_plugin_framework_e2e!
  assert!(File.exist?(PLUGIN_FRAMEWORK_EXAMPLE),
          "plugin_framework example not found: #{PLUGIN_FRAMEWORK_EXAMPLE}")
  # Deliberately NO stdlib args: this is the exact command a user types, so it
  # guards the `run` one-file DX (prepended core stdlib) from regressing.
  stdout, stderr, status = run_compiler("run", PLUGIN_FRAMEWORK_EXAMPLE.to_s)
  assert!(status.success?,
          "l run plugin_framework.l failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  combined = (stdout || "") + (stderr || "")
  assert!(!combined.include?("[ERROR]"), "l run emitted diagnostics\n#{combined}")
  normalized = stdout.gsub("\r\n", "\n")
  expected = "app version: 0.1.0\n" \
             "2\n" \
             "[start] logger ready\n[start] auth ready\n" \
             "[start] logger ready\n" \
             "auth\n" \
             "logger\nauth\n"
  assert!(normalized == expected, "expected #{expected.inspect}, got #{normalized.inspect}")
  puts "  OK l run plugin_framework.l (one-file DX, no explicit stdlib args)"
end

def test_stdin_compile_e2e!
  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("test_eval.exe")
  FileUtils.rm_f(exe)

  # Pipe source into `-` (in-memory / <stdin>), no temp .l file.
  src = "fun main() -> i32 {\nprint(123)\nreturn 0\n}\n"
  stdout, stderr, status = run_compiler(
    *runtime_l_files, "-", "-o", exe.to_s,
    stdin_data: src
  )
  assert!(status.success?, "stdin compile failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "test_eval.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "test_eval.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "123\n", "expected \"123\\n\", got #{normalized.inspect}")

  # Intentional syntax error: diagnostics must use virtual file <stdin>.
  bad = "fun main() -> i32 { print(123\n"
  bad_out, bad_err, bad_st = run_compiler("--dump-ast", "-", stdin_data: bad)
  assert!(!bad_st.success?, "expected stdin syntax error to fail, got success\n#{bad_err}\n#{bad_out}")
  combined = "#{bad_err}\n#{bad_out}"
  assert!(
    combined.match?(/\[ERROR\] <stdin>:\d+:\d+/),
    "expected [ERROR] <stdin>:line:col, got:\n#{combined}"
  )

  # `l eval` without -o: ORC JIT in-process (no .exe); stdlib auto-prepended.
  eval_src = "fun main() -> i32 { print(456); return 0; }"
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  ev_out, ev_err, ev_st = run_compiler("eval", eval_src)
  t1 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  assert!(ev_st.success?, "l eval failed (#{ev_st.exitstatus})\n#{ev_err}\n#{ev_out}")
  assert!(
    ev_out.gsub("\r\n", "\n") == "456\n",
    "expected 456 from l eval, got #{ev_out.inspect}"
  )
  jit_ms = (t1 - t0) * 1000.0
  assert!(jit_ms < 5000.0, "l eval too slow: #{format('%.1f', jit_ms)}ms (expected <5s)")

  puts "  OK stdin / l eval in-memory compile + ORC JIT (<stdin> / <eval>)"
end

def test_lcmd_l_callback_e2e!
  assert!(File.exist?(LCMD_L_CALLBACK), "lcmd_l_callback case not found: #{LCMD_L_CALLBACK}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("lcmd_l_callback.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(LCMD_L_CALLBACK))
  assert!(status.success?, "compile lcmd_l_callback failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "lcmd_l_callback.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "lcmd_l_callback.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\n", "expected \"42\\n\", got #{normalized.inspect}")

  puts "  OK lcmd_l_callback e2e (LCMD → L handler → AST inject)"
end

def test_lcmd_path_e2e!
  assert!(File.exist?(LCMD_PATH), "lcmd_path case not found: #{LCMD_PATH}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("lcmd_path.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(LCMD_PATH))
  assert!(status.success?, "compile lcmd_path failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "lcmd_path.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "lcmd_path.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "42\n", "expected \"42\\n\", got #{normalized.inspect}")

  puts "  OK lcmd_path e2e (hierarchical path → class inject)"
end

def test_uid_counter_e2e!
  assert!(File.exist?(UID_TEST), "uid_test case not found: #{UID_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("uid_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(UID_TEST))
  assert!(status.success?, "compile uid_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "uid_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "uid_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "200\n", "expected \"200\\n\", got #{normalized.inspect}")

  puts "  OK uid_counter e2e (LCMD uid_counter → temp_0/temp_1 → 200)"
end

def test_uid_counter_bad_rejected!
  assert!(File.exist?(UID_BAD), "uid_bad case not found: #{UID_BAD}")

  stdout, stderr, status = run_compiler("--emit-llvm", *with_runtime(UID_BAD))
  assert!(!status.success?, "uid_bad.l should fail to compile\n#{stdout}\n#{stderr}")
  combined = "#{stdout}\n#{stderr}"
  assert!(
    combined.match?(/\[ERROR\].*uid_counter\(\) is only allowed in compile-time context/m),
    "expected compile-time-only diagnostic, got:\n#{combined}"
  )

  puts "  OK uid_counter rejected at runtime (compile-time only)"
end

def test_context_macro_e2e!
  assert!(File.exist?(CONTEXT_MACRO_TEST), "context_macro_test case not found: #{CONTEXT_MACRO_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("context_macro_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s, *with_runtime(STDLIB_STRING, CONTEXT_MACRO_TEST)
  )
  assert!(status.success?, "compile context_macro_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "context_macro_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "context_macro_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")
  normalized = out.gsub("\r\n", "\n")
  lines = normalized.split("\n", -1)
  assert!(lines.length >= 3, "expected >=3 lines, got #{normalized.inspect}")
  assert!(
    lines[0].include?("File: ") && lines[0].include?("context_macro_test.l"),
    "File line missing path, got #{lines[0].inspect}"
  )
  expected_line = File.readlines(CONTEXT_MACRO_TEST).each_with_index.find { |l, _|
    l.include?("__LINE__")
  }[1] + 1
  assert!(
    lines[1] == "Line: #{expected_line}",
    "Line expected \"Line: #{expected_line}\", got #{lines[1].inspect}"
  )
  assert!(lines[2] == "Func: test_context", "Func line expected, got #{lines[2].inspect}")

  puts "  OK context macros e2e (__FILE__ / __LINE__ / __FUNC__)"
end

def test_comptime_ok_e2e!
  assert!(File.exist?(COMPTIME_OK_TEST), "comptime_ok_test case not found: #{COMPTIME_OK_TEST}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("comptime_ok_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(COMPTIME_OK_TEST))
  assert!(status.success?, "compile comptime_ok_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "comptime_ok_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "comptime_ok_test.exe exited #{st.exitstatus}\n#{err}\n#{out}")

  puts "  OK comptime block e2e (compile-time check passed)"
end

def test_comptime_fail_rejected!
  assert!(File.exist?(COMPTIME_FAIL_TEST), "comptime_fail_test case not found: #{COMPTIME_FAIL_TEST}")

  stdout, stderr, status = run_compiler("--emit-llvm", *with_runtime(COMPTIME_FAIL_TEST))
  assert!(!status.success?, "comptime_fail_test.l should fail to compile\n#{stdout}\n#{stderr}")
  combined = "#{stdout}\n#{stderr}"
  assert!(
    combined.match?(/\[ERROR\].*Build forbidden/m),
    "expected Build forbidden diagnostic, got:\n#{combined}"
  )
  assert!(
    combined.match?(/Compile-time error:/m),
    "expected Compile-time error prefix, got:\n#{combined}"
  )

  puts "  OK compile_error rejected at comptime (Build forbidden)"
end

def test_map_e2e!
  assert!(File.exist?(MAP_TEST), "map_test case not found: #{MAP_TEST}")
  assert!(File.exist?(STDLIB_MAP), "stdlib map not found: #{STDLIB_MAP}")
  assert!(File.exist?(STDLIB_OPTIONAL), "stdlib optional not found: #{STDLIB_OPTIONAL}")
  assert!(File.exist?(STDLIB_STRING), "stdlib string not found: #{STDLIB_STRING}")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("map_test.exe")
  FileUtils.rm_f(exe)

  stdout, stderr, status = run_compiler(
    "-o", exe.to_s,
    *with_runtime(STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, MAP_TEST)
  )
  assert!(status.success?, "compile map_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "map_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "map_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "2\n99\n", "expected \"2\\n99\\n\", got #{normalized.inspect}")

  puts "  OK map_test e2e (Map<i32> String keys)"
end

def test_ref_bad_readonly!
  assert!(File.exist?(REF_BAD), "ref_bad case not found: #{REF_BAD}")

  stdout, stderr, status = run_compiler("--emit-llvm", REF_BAD.to_s)
  assert!(!status.success?, "ref_bad was expected to fail compilation, but exited 0\n#{stdout}\n#{stderr}")
  combined = "#{stdout}\n#{stderr}"
  assert!(
    combined.match?(/\[ERROR\] .+ref_bad\.l:\d+:\d+ - .*read-only parameter/im),
    "ref_bad missing located [ERROR] read-only diagnostic, got:\n#{combined}"
  )

  puts "  OK ref_bad rejects mutation of read-only class param"
end

def host_clang
  ENV.fetch("L_CLANG") do
    # Prefer the same clang the compiler was built with (PATH may already include LLVM).
    candidates = [
      "D:/Librarys/clang+llvm-22.1.0-x86_64-pc-windows-msvc/bin/clang.exe",
      "clang"
    ]
    candidates.find { |c| c == "clang" || File.exist?(c) } || "clang"
  end
end

def host_ar
  ENV.fetch("L_AR") do
    candidates = [
      "D:/Librarys/clang+llvm-22.1.0-x86_64-pc-windows-msvc/bin/llvm-ar.exe",
      "llvm-ar",
      "ar"
    ]
    candidates.find { |c| c == "llvm-ar" || c == "ar" || File.exist?(c) } || "llvm-ar"
  end
end

def build_math_lib_static!
  vendor = PKG_TEST.join("vendor", "math_lib")
  src = vendor.join("math_lib.c")
  obj = vendor.join("math_lib.o")
  lib = vendor.join("libmath_lib.a")
  assert!(File.exist?(src), "math_lib.c missing: #{src}")

  FileUtils.rm_f(obj)
  FileUtils.rm_f(lib)
  out, err, st = Open3.capture3(host_clang, "-c", src.to_s, "-o", obj.to_s)
  assert!(st.success?, "clang -c math_lib.c failed\n#{err}\n#{out}")
  out2, err2, st2 = Open3.capture3(host_ar, "rcs", lib.to_s, obj.to_s)
  assert!(st2.success?, "ar rcs libmath_lib.a failed\n#{err2}\n#{out2}")
  assert!(File.exist?(lib), "static library was not produced: #{lib}")
end

def test_pkg_resolve_e2e!
  assert!(Dir.exist?(PKG_TEST), "pkg_test dir not found: #{PKG_TEST}")
  assert!(File.exist?(PKG_TEST.join("l.toml")), "pkg_test l.toml missing")
  assert!(File.exist?(PKG_TEST.join("main.l")), "pkg_test main.l missing")
  assert!(File.exist?(PKG_TEST.join("vendor", "math_lib", "math_lib.li")), "math_lib.li missing")

  build_math_lib_static!

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("pkg_test.exe")
  FileUtils.rm_f(exe)

  main_l = PKG_TEST.join("main.l")
  stdout, stderr, status = run_compiler("-o", exe.to_s, *with_runtime(main_l))
  assert!(status.success?, "compile pkg_test failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "pkg_test.exe was not produced")

  out, err, st = Open3.capture3(exe.to_s)
  assert!(st.success?, "pkg_test.exe exited #{st.exitstatus}\n#{err}")
  normalized = out.gsub("\r\n", "\n")
  assert!(normalized == "30\n", "expected \"30\\n\", got #{normalized.inspect}")

  puts "  OK pkg_test e2e (l.toml + vendor math_lib → 30)"
end

def test_pkg_cycle_rejected!
  assert!(Dir.exist?(PKG_CYCLE), "pkg_cycle dir not found: #{PKG_CYCLE}")
  main_l = PKG_CYCLE.join("main.l")
  stdout, stderr, status = run_compiler("-o", OUT_DIR.join("pkg_cycle.exe").to_s, main_l.to_s)
  assert!(!status.success?, "pkg_cycle should fail to compile")
  combined = "#{stderr}\n#{stdout}"
  assert!(
    combined.include?("[ERROR] Circular dependency detected between '") &&
      combined.include?("pkg_a") && combined.include?("pkg_b"),
    "expected circular dependency error between pkg_a/pkg_b, got:\n#{combined}"
  )
  puts "  OK pkg_cycle rejected (circular l.toml deps)"
end

def test_code_as_config_e2e!
  assert!(Dir.exist?(CODE_AS_CONFIG), "code_as_config dir not found: #{CODE_AS_CONFIG}")
  lconf = CODE_AS_CONFIG.join("lconf.l")
  main_l = CODE_AS_CONFIG.join("main.l")
  assert!(File.exist?(lconf), "lconf.l missing")
  assert!(File.exist?(main_l), "main.l missing")

  FileUtils.mkdir_p(OUT_DIR)
  exe = OUT_DIR.join("code_as_config.exe")
  FileUtils.rm_f(exe)

  # --release → get_build_mode() == "Release" → skip debug_utils branch.
  stdout, stderr, status = run_compiler("--release", "-o", exe.to_s, *with_runtime(main_l))
  assert!(status.success?, "compile code_as_config failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  assert!(File.exist?(exe), "code_as_config.exe was not produced")

  combined = "#{stdout}\n#{stderr}".gsub("\r\n", "\n")
  assert!(
    combined.include?("加载依赖: crypto, network"),
    "expected JIT lconf load line \"加载依赖: crypto, network\", got:\n#{combined}"
  )
  assert!(
    !combined.include?("debug_utils"),
    "Release build should not load debug_utils, got:\n#{combined}"
  )

  puts "  OK code_as_config (JIT lconf.l → 加载依赖: crypto, network)"
end

def main
  suite = %i[
    test_golden_dump_tokens!
    test_var_decl_dump_ast!
    test_expr_dump_ast!
    test_func_call_dump_ast!
    test_func_def_dump_ast!
    test_codegen_simple_emit_llvm!
    test_rule_refs_consistent!
    test_run_hello_e2e!
    test_control_flow_e2e!
    test_class_basic_e2e!
    test_delegate_e2e!
    test_delegate_conflict_e2e!
    test_delegate_conflict_bad_rejected!
    test_delegate_same_type_bad_rejected!
    test_delegate_same_type_partial_bad_rejected!
    test_static_method_e2e!
    test_static_method_bad_rejected!
    test_interface_e2e!
    test_dynamic_dispatch_e2e!
    test_gen_interface_string!
    test_string_e2e!
    test_print_format_e2e!
    test_generic_e2e!
    test_generic_constraint_e2e!
    test_heap_defer_e2e!
    test_auto_free_e2e!
    test_sync_race!
    test_thread_e2e!
    test_concurrency_stress_e2e!
    test_atomic_stress_e2e!
    test_net_e2e!
    test_http_server_e2e!
    test_file_io_e2e!
    test_fs_e2e!
    test_time_e2e!
    test_time_format_e2e!
    test_timer_e2e!
    test_string_ext_e2e!
    test_path_fs_ext_e2e!
    test_process_capture_e2e!
    test_json_e2e!
    test_join_e2e!
    test_log_config_e2e!
    test_http_stdlib_e2e!
    test_regex_e2e!
    test_regex_overflow_e2e!
    test_process_e2e!
    test_cli_args_e2e!
    test_cli_flags_e2e!
    test_env_client_e2e!
    test_pkg_resolve_e2e!
    test_pkg_cycle_rejected!
    test_code_as_config_e2e!
    test_ffi_export_shared!
    test_import_module_e2e!
    test_array_e2e!
    test_array_sugar_e2e!
    test_array_poly_e2e!
    test_match_e2e!
    test_match_expr_e2e!
    test_implicit_boxing_e2e!
    test_coalesce_boxing_e2e!
    test_variant_unbox_return_e2e!
    test_comment_pollution_e2e!
    test_optional_unwrap_chain_e2e!
    test_optional_iface_chain_e2e!
    test_cast_e2e!
    test_downcast_e2e!
    test_partial_e2e!
    test_property_fallback_e2e!
    test_field_syntax_e2e!
    test_field_syntax_trailing_underscore_rejected!
    test_nested_init_e2e!
    test_clone_e2e!
    test_global_e2e!
    test_global_class_e2e!
    test_lambda_e2e!
    test_lambda_capture_e2e!
    test_lambda_state_e2e!
    test_closure_escape_good_e2e!
    test_compound_assign_e2e!
    test_inc_dec_e2e!
    test_memory_stress_e2e!
    test_memory_leak_hard_e2e!
    test_multi_file_e2e!
    test_arena_e2e!
    test_string_interp_e2e!
    test_type_promotion_e2e!
    test_destructure_e2e!
    test_variant_e2e!
    test_variant_ext_e2e!
    test_lexer_demo_e2e!
    test_mini_parser_e2e!
    test_config_e2e!
    test_sort_e2e!
    test_for_in_e2e!
    test_generic_inference_e2e!
    test_optional_e2e!
    test_coalesce_e2e!
    test_coalesce_non_optional_bad!
    test_properties_e2e!
    test_http_parse_e2e!
    test_break_continue_e2e!
    test_break_outside_loop_rejected!
    test_plugin_framework_e2e!
    test_this_in_static_rejected!
    test_chained_call_e2e!
    test_delegate_optional_e2e!
    test_delegate_private_bad_rejected!
    test_sync_try_release!
    test_result_e2e!
    test_try_operator_e2e!
    test_lifecycle_e2e!
    test_to_string_e2e!
    test_constructor_e2e!
    test_string_concat_e2e!
    test_kw_mixed_e2e!
    test_kw_bad_parse!
    test_input_readline_e2e!
    test_extern_strlen_e2e!
    test_callback_e2e!
    test_ref_pass_e2e!
    test_ref_bad_readonly!
    test_map_e2e!
    test_lcmd_l_callback_e2e!
    test_lcmd_path_e2e!
    test_uid_counter_e2e!
    test_uid_counter_bad_rejected!
    test_context_macro_e2e!
    test_comptime_ok_e2e!
    test_comptime_fail_rejected!
    test_l_doc_markdown!
    test_node_hook_optin_e2e!
    test_stdin_compile_e2e!
    test_repl_e2e!
    test_eval_jit_e2e!
    test_cli_run_e2e!
    test_cli_run_plugin_framework_e2e!
  ]

  force_all, filter = parse_runner_args!(ARGV)
  filter = nil if filter && filter.empty?

  $stdout.sync = true
  $stderr.sync = true

  selected = suite.select { |name| test_matches_filter?(name, filter) }
  if selected.empty?
    warn color("no tests matched filter=#{filter.inspect}", RED)
    exit 2
  end

  mode = force_all ? "ALL (cache ignored)" : "INCREMENTAL (skip unchanged PASS)"
  mode += " filter=#{filter.inspect}" if filter

  cache = load_test_cache
  # Frozen view for skip decisions. `record_test_pass!` refreshes `cache["files"]`
  # as the session progresses; decisions must always compare against the state
  # the session STARTED with, otherwise a test that re-ran earlier in this run
  # silently "blesses" changed dependencies for every later test.
  decision_cache = deep_copy_cache(cache)

  passed = 0
  failed = 0
  skipped = 0
  total = selected.size

  puts color("Running #{total}/#{suite.size} tests — #{mode}", GREEN)
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)

  selected.each_with_index do |name, i|
    n = i + 1
    key = name.to_s
    entry = cache["tests"][key]

    if !force_all && test_can_skip?(name, decision_cache["tests"][key], decision_cache)
      puts color("[#{n}/#{total}] [SKIP] #{name}", YELLOW)
      skipped += 1
      next
    end

    print color("[#{n}/#{total}] ", GREEN)
    print "> #{name} ... "
    $stdout.flush
    t_case = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    begin
      send(name)
      elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t_case
      puts color(format("PASS (%.1fs)", elapsed), GREEN)
      passed += 1
      record_test_pass!(name, cache)
    rescue TestFailed => e
      # Fail-fast: stop the suite on the first red failure.
      elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t_case
      puts color(format("[FAIL] (%.1fs)", elapsed), RED)
      cache["tests"].delete(key)
      save_test_cache(cache)
      total_elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
      puts color(
        format(
          "STOPPED on first failure: %d passed, 1 failed, %d skipped (%.1fs)",
          passed, skipped, total_elapsed
        ),
        RED
      )
      warn color("fail-fast: remaining tests not run", YELLOW)
      warn e.message unless e.message.to_s.empty?
      exit 1
    end
  end

  save_test_cache(cache)

  total_elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
  puts color(
    format("%d passed, %d failed, %d skipped (%.1fs)", passed, failed, skipped, total_elapsed),
    GREEN
  )
  exit 0
end

main if $PROGRAM_NAME == __FILE__
