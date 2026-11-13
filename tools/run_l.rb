#!/usr/bin/env ruby
# frozen_string_literal: true

# One-shot compile + run for L sources.
#
# Usage:
#   ruby tools/run_l.rb tests/cases/to_string_test.l
#   ruby tools/run_l.rb -O3 examples/mini_lexer.l
#   ruby tools/run_l.rb --stdlib tests/cases/map_test.l
#   ruby tools/run_l.rb --emit-llvm tests/cases/class_basic.l
#   ruby tools/run_l.rb -o my.exe file.l --no-run
#
# Always prepends stdlib/memory.l + io.l + thread.l.
# Extra stdlib modules are auto-detected from source text unless --stdlib / --no-auto.
#
# Env: L_COMPILER  (default: build/clang-debug/compiler/l.exe)

require "fileutils"
require "open3"
require "optparse"
require "pathname"
require "shellwords"

ROOT = Pathname.new(__dir__).parent.expand_path
OUT_DIR = ROOT.join("build", "clang-debug", "run_out")
COMPILER = ENV.fetch(
  "L_COMPILER",
  ROOT.join("build", "clang-debug", "compiler", "l.exe").to_s
)

STDLIB = {
  memory: ROOT.join("stdlib", "memory.l"),
  io: ROOT.join("stdlib", "io.l"),
  thread: ROOT.join("stdlib", "thread.l"),
  array: ROOT.join("stdlib", "array.l"),
  string: ROOT.join("stdlib", "string.l"),
  optional: ROOT.join("stdlib", "optional.l"),
  result: ROOT.join("stdlib", "result.l"),
  map: ROOT.join("stdlib", "map.l"),
  variant: ROOT.join("stdlib", "variant.l"),
  file: ROOT.join("stdlib", "file.l"),
  fs: ROOT.join("stdlib", "fs.l"),
  env: ROOT.join("stdlib", "env.l"),
  cli: ROOT.join("stdlib", "cli.l"),
  time: ROOT.join("stdlib", "time.l"),
  regex: ROOT.join("stdlib", "regex.l"),
  process: ROOT.join("stdlib", "process.l")
}.freeze

# Emit order matters: Array before String; Optional before Map/Variant/File; Result before fs/process.
STDLIB_ORDER = %i[memory io thread array string optional result map variant file fs env cli time regex process].freeze

RUNTIME_KEYS = %i[memory io thread].freeze

def die!(msg)
  warn "FAIL: #{msg}"
  exit 1
end

def usage!(opts)
  warn opts
  exit 2
end

options = {
  opt: nil,
  release: false,
  emit_llvm: false,
  emit_obj: false,
  dump_tokens: false,
  dump_ast: false,
  output: nil,
  no_run: false,
  keep: false,
  dry_run: false,
  stdlib_all: false,
  no_auto: false,
  extra_stdlib: []
}

parser = OptionParser.new do |o|
  o.banner = "Usage: ruby tools/run_l.rb [options] <source.l> [more.l ...]"
  o.on("-OLEVEL", /[0-3]/, "Optimization level -O0..-O3") { |v| options[:opt] = "-O#{v}" }
  o.on("--release", "Release build mode") { options[:release] = true }
  o.on("--emit-llvm", "Print LLVM IR instead of linking/running") { options[:emit_llvm] = true }
  o.on("--emit-obj", "Emit object file only (implies --no-run)") { options[:emit_obj] = true }
  o.on("--dump-tokens", "Dump tokens and exit") { options[:dump_tokens] = true }
  o.on("--dump-ast", "Dump AST and exit") { options[:dump_ast] = true }
  o.on("-o PATH", "Output path (.exe / .o / .ll dump file unused)") { |v| options[:output] = v }
  o.on("--no-run", "Compile only; do not execute") { options[:no_run] = true }
  o.on("--keep", "Keep the produced executable after run") { options[:keep] = true }
  o.on("--dry-run", "Print the compiler command and exit") { options[:dry_run] = true }
  o.on("--stdlib", "Include all stdlib modules") { options[:stdlib_all] = true }
  o.on("--no-auto", "Do not auto-detect stdlib from source") { options[:no_auto] = true }
  o.on("--with LIST", "Comma-separated stdlib names (array,string,optional,result,map,variant)") do |v|
    options[:extra_stdlib].concat(v.split(",").map { |s| s.strip.downcase.to_sym })
  end
  o.on("-h", "--help", "Show help") { usage!(o) }
end

begin
  parser.parse!
rescue OptionParser::ParseError => e
  die!(e.message)
end

sources = ARGV.map { |a| Pathname.new(a).expand_path }
usage!(parser) if sources.empty?

sources.each do |p|
  die!("source not found: #{p}") unless p.file?
  die!("expected .l source: #{p}") unless p.extname.downcase == ".l"
end

die!("compiler not found: #{COMPILER}\nBuild with: Invoke-LlvmEnv 'cmake --build build/clang-debug --target l'") unless File.exist?(COMPILER)

def read_sources(paths)
  paths.map { |p| File.read(p, mode: "r:bom|utf-8") }.join("\n")
end

def detect_stdlib(text)
  need = {}
  need[:string] = true if text.match?(/\bString\b|\bto_string\b|\.concat\b|\.split\b|from_string\b/)
  need[:array] = true if text.match?(/\bArray\b|\[\s*[^\]]|^\s*\[/m) || need[:string]
  need[:optional] = true if text.match?(/\bOptional\b|\.unwrap\b|\.is_some\b|\.is_none\b/)
  need[:result] = true if text.match?(/\bResult\b|\.is_ok\b|\.is_err\b|\bOk\b|\bErr\b/)
  # Try operator `expr?` is common with Result; keep conservative (word boundary after ? is rare).
  need[:result] = true if text.match?(/\?\s*(?:;|\n|\)|\}|,)/)
  need[:map] = true if text.match?(/\bMap\b/)
  need[:variant] = true if text.match?(/\bVariant\b|from_i32\b|as_i32\b|is_i32\b/)
  need[:file] = true if text.match?(/\bFile\b|read_all\b|write_all\b|l_file_open\b|l_file_read\b|l_file_write\b/)
  need[:fs] = true if text.match?(/\bfile_exists\b|\bcreate_dir\b|\bremove_file\b|\bremove_dir\b|l_file_exists\b|l_create_dir\b|l_remove_file\b|l_remove_dir\b/)
  need[:env] = true if text.match?(/\bget_args\b|\bget_env\b|l_get_arg\b|l_get_env\b/)
  need[:cli] = true if text.match?(/\bparse_flags\b/)
  need[:time] = true if text.match?(/\btime_now\b|\btime_now_ms\b|\bformat_time\b|\bsleep\b|l_time_now|l_format_time\b/)
  need[:regex] = true if text.match?(/\bcontains_pattern\b|l_regex_match\b/)
  need[:process] = true if text.match?(/\brun_command\b|l_run_command\b/)
  # Map/Variant/File/Env usually need String + Optional.
  if need[:map] || need[:variant] || need[:file] || need[:env]
    need[:string] = true
    need[:array] = true
    need[:optional] = true
  end
  if need[:cli]
    need[:string] = true
    need[:array] = true
    need[:optional] = true
    need[:map] = true
    need[:env] = true
  end
  if need[:fs] || need[:process]
    need[:string] = true
    need[:array] = true
    need[:result] = true
  end
  if need[:regex] || need[:time]
    need[:string] = true
    need[:array] = true
  end
  if need[:result]
    need[:string] = true
    need[:array] = true
  end
  need
end

needed = {}
RUNTIME_KEYS.each { |k| needed[k] = true }

if options[:stdlib_all]
  STDLIB_ORDER.each { |k| needed[k] = true }
else
  unless options[:no_auto]
    detect_stdlib(read_sources(sources)).each_key { |k| needed[k] = true }
  end
  options[:extra_stdlib].each do |k|
    die!("unknown stdlib module: #{k}") unless STDLIB.key?(k)
    needed[k] = true
    # Keep dependency chain coherent when user forces a module.
    if %i[string result map variant].include?(k)
      needed[:array] = true
    end
    if %i[map variant].include?(k)
      needed[:string] = true
      needed[:optional] = true
    end
    if k == :result
      needed[:string] = true
    end
  end
end

stdlib_files = STDLIB_ORDER.select { |k| needed[k] }.map do |k|
  path = STDLIB[k]
  die!("stdlib missing: #{path}") unless path.file?
  path
end

# Drop user paths that already appear as stdlib (avoid duplicate AST).
stdlib_set = stdlib_files.map(&:expand_path).map(&:to_s)
user_sources = sources.reject { |p| stdlib_set.include?(p.expand_path.to_s) }
die!("no user .l sources left after filtering stdlib duplicates") if user_sources.empty?

dump_only = options[:dump_tokens] || options[:dump_ast] || options[:emit_llvm]
options[:no_run] = true if options[:emit_obj] || dump_only

FileUtils.mkdir_p(OUT_DIR)
base = user_sources.last.basename(".l").to_s
default_exe = OUT_DIR.join("#{base}.exe")
default_obj = OUT_DIR.join("#{base}.o")

out_path =
  if options[:output]
    Pathname.new(options[:output]).expand_path
  elsif options[:emit_obj]
    default_obj
  elsif dump_only
    nil
  else
    default_exe
  end

cmd = [COMPILER]
cmd << options[:opt] if options[:opt]
cmd << "--release" if options[:release]
cmd << "--dump-tokens" if options[:dump_tokens]
cmd << "--dump-ast" if options[:dump_ast]
cmd << "--emit-llvm" if options[:emit_llvm]
cmd << "--emit-obj" if options[:emit_obj]
if out_path && !options[:dump_tokens] && !options[:dump_ast] && !options[:emit_llvm]
  cmd << "-o" << out_path.to_s
  FileUtils.rm_f(out_path)
end
cmd.concat(stdlib_files.map(&:to_s))
cmd.concat(user_sources.map(&:to_s))

puts "compiler: #{COMPILER}"
puts "stdlib:   #{stdlib_files.map { |p| p.relative_path_from(ROOT) }.join(' ')}"
puts "sources:  #{user_sources.map { |p| p.relative_path_from(ROOT) }.join(' ')}"
puts "cmd:      #{Shellwords.join(cmd)}"

if options[:dry_run]
  puts "dry-run: not compiling"
  exit 0
end

stdout, stderr, status = Open3.capture3(*cmd)
$stdout.print stdout
$stderr.print stderr
die!("compile failed (#{status.exitstatus})") unless status.success?

if dump_only
  exit 0
end

die!("output missing: #{out_path}") if out_path && !out_path.exist?

if options[:no_run]
  puts "OK compiled → #{out_path}"
  exit 0
end

puts "----- run #{out_path} -----"
run_out, run_err, run_st = Open3.capture3(out_path.to_s)
$stdout.print run_out
$stderr.print run_err
unless options[:keep]
  FileUtils.rm_f(out_path)
end
exit run_st.exitstatus
