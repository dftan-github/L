#!/usr/bin/env ruby
# frozen_string_literal: true

# L performance runner — isolated from tests/run_tests.rb (rule 51).
# Compiles with -O3 and reports wall-clock runtime via Process.clock_gettime.

require "fileutils"
require "open3"
require "pathname"

ROOT = Pathname.new(__dir__).parent.expand_path
BENCH_DIR = ROOT.join("benchmarks")
OUT_DIR = ROOT.join("build", "clang-debug", "bench_out")
COMPILER = ENV.fetch(
  "L_COMPILER",
  ROOT.join("build", "clang-debug", "compiler", "l.exe").to_s
)
STDLIB_MEMORY = ROOT.join("stdlib", "memory.l")
STDLIB_IO = ROOT.join("stdlib", "io.l")
LOOP_BENCH = BENCH_DIR.join("loop_bench.l")
TREE_BENCH = BENCH_DIR.join("property_tree_bench.l")
STDLIB_STRING = ROOT.join("stdlib", "string.l")
STDLIB_ARRAY = ROOT.join("stdlib", "array.l")
STDLIB_OPTIONAL = ROOT.join("stdlib", "optional.l")
STDLIB_MAP = ROOT.join("stdlib", "map.l")
STDLIB_VARIANT = ROOT.join("stdlib", "variant.l")
STDLIB_PROPERTIES = ROOT.join("stdlib", "properties.l")
STDLIB_ENV = ROOT.join("stdlib", "env.l")

def die!(msg)
  warn "FAIL: #{msg}"
  exit 1
end

def compile_o3!(sources, exe)
  FileUtils.mkdir_p(OUT_DIR)
  FileUtils.rm_f(exe)
  cmd = [COMPILER, "-O3", "-o", exe.to_s, STDLIB_MEMORY.to_s, STDLIB_IO.to_s, *sources.map(&:to_s)]
  stdout, stderr, status = Open3.capture3(*cmd)
  unless status.success?
    die!("compile failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  end
  die!("exe missing: #{exe}") unless File.exist?(exe)
end

def time_exe!(exe)
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  stdout, stderr, status = Open3.capture3(exe.to_s)
  t1 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  unless status.success?
    die!("run failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  end
  [(t1 - t0) * 1000.0, stdout]
end

die!("compiler not found: #{COMPILER}") unless File.exist?(COMPILER)
die!("loop_bench.l not found: #{LOOP_BENCH}") unless File.exist?(LOOP_BENCH)

exe = OUT_DIR.join("loop_bench.exe")
puts "Compiling loop_bench.l with -O3 ..."
compile_o3!([LOOP_BENCH], exe)

puts "Running #{exe} ..."
ms, out = time_exe!(exe)
normalized = out.to_s.gsub("\r\n", "\n")
puts normalized
puts format("wall_time_ms=%.3f", ms)

# CPropertys: recursive tree with a lazily created `Map<ptr>` child index.
tree_exe = OUT_DIR.join("property_tree_bench.exe")
puts "Compiling property_tree_bench.l with -O3 ..."
compile_o3!([STDLIB_ARRAY, STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP, STDLIB_VARIANT,
             STDLIB_PROPERTIES, STDLIB_ENV, TREE_BENCH], tree_exe)

%w[build lookup deep].each do |mode|
  puts "Running #{tree_exe} #{mode} ..."
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  out, err, status = Open3.capture3(tree_exe.to_s, mode)
  t1 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  die!("property_tree #{mode} failed (#{status.exitstatus})\n#{err}\n#{out}") unless status.success?
  puts out.to_s.gsub("\r\n", "\n")
  puts format("wall_time_ms=%.3f", (t1 - t0) * 1000.0)
end

# ---------------------------------------------------------------------------
# L vs C++ head-to-head suite (benchmarks/bench_l.l vs benchmarks/bench_cpp.cpp)
# Three aligned workloads — A dense arithmetic loop, B heap object creation,
# C string-keyed map — each printing its own elapsed milliseconds.
#
# IMPORTANT: bench_l.l is compiled AOT to a native .exe with -O3 and executed as
# a process. It never goes through `l eval`, `l run`'s REPL, or the comptime
# tree-walking interpreter — the comparison must measure real LLVM -O3 machine
# code, exactly like the C++ side.
# ---------------------------------------------------------------------------
CXX_BENCH = BENCH_DIR.join("bench_cpp.cpp")
L_BENCH = BENCH_DIR.join("bench_l.l")

def clangxx
  return ENV["L_CXX"] if ENV["L_CXX"] && !ENV["L_CXX"].empty?

  # Prefer the LLVM toolchain this project is built against (its clang++ uses the
  # MSVC STL correctly), then the one next to l.exe's LLVM, then PATH.
  # Sort newest-first so an older LLVM that can't parse the current MSVC STL
  # isn't picked just because its directory name sorts first.
  versioned = Dir.glob("D:/Librarys/clang*/bin/clang++.exe").sort_by do |p|
    p[/clang\+llvm-(\d+)\.(\d+)\.(\d+)/] ? [$1.to_i, $2.to_i, $3.to_i] : [0, 0, 0]
  end.reverse
  candidates = versioned + Dir.glob(ROOT.join("..", "**", "clang++.exe").to_s)
  candidates.find { |p| File.exist?(p) } ||
    `where clang++ 2>NUL`.lines.first&.strip
end

def compile_cpp_o3!(src, exe)
  cxx = clangxx
  die!("clang++ not found (set L_CXX)") unless cxx && File.exist?(cxx.to_s)
  FileUtils.mkdir_p(OUT_DIR)
  FileUtils.rm_f(exe)
  cmd = [cxx.to_s, "-O3", "-std=c++17", "-o", exe.to_s, src.to_s]
  stdout, stderr, status = Open3.capture3(*cmd)
  unless status.success?
    die!("C++ compile failed (#{status.exitstatus})\n#{stderr}\n#{stdout}")
  end
  die!("exe missing: #{exe}") unless File.exist?(exe)
end

die!("bench_cpp.cpp not found: #{CXX_BENCH}") unless File.exist?(CXX_BENCH)
die!("bench_l.l not found: #{L_BENCH}") unless File.exist?(L_BENCH)

cpp_exe = OUT_DIR.join("bench_cpp.exe")
l_bench_exe = OUT_DIR.join("bench_l.exe")

puts "\nCompiling bench_cpp.cpp with -O3 ..."
compile_cpp_o3!(CXX_BENCH, cpp_exe)

puts "Compiling bench_l.l with -O3 ..."
compile_o3!([STDLIB_ARRAY, STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP,
             ROOT.join("stdlib", "thread.l"), ROOT.join("stdlib", "time.l"),
             L_BENCH], l_bench_exe)

def parse_ms(text)
  text.to_s.gsub("\r\n", "\n").lines.filter_map do |ln|
    m = ln.match(/\A\s*([ABC]) .*?\s([\d.]+) ms/)
    [m[1], m[2].to_f] if m
  end.to_h
end

cpp_runs = []
l_runs = []
3.times do |round|
  puts "\nRound #{round + 1}"
  puts "--- C++ (bench_cpp.exe) ---"
  _, out = time_exe!(cpp_exe)
  puts out.to_s.gsub("\r\n", "\n")
  cpp_runs << parse_ms(out)

  puts "--- L (bench_l.exe) ---"
  _, out = time_exe!(l_bench_exe)
  puts out.to_s.gsub("\r\n", "\n")
  l_runs << parse_ms(out)
end

# Median of the 3 rounds — steady-state timing, robust to one noisy run.
def median(values)
  sorted = values.sort
  sorted[sorted.size / 2]
end

cpp_ms = %w[A B C].to_h { |k| [k, median(cpp_runs.map { |r| r[k] }.compact)] }
l_ms = %w[A B C].to_h { |k| [k, median(l_runs.map { |r| r[k] }.compact)] }

puts "\n| test | C++ (ms) | L (ms) | L / C++ |"
puts "|------|---------:|-------:|--------:|"
%w[A B C].each do |k|
  c = cpp_ms[k]
  l = l_ms[k]
  next unless c && l
  if c < 0.0005
    puts format("| %s | %.3f | %.3f | n/a (C++ loop elided) |", k, c, l)
    next
  end
  ratio = l / c
  puts format("| %s | %.3f | %.3f | %.2fx |", k, c, l, ratio)
end

# ---------------------------------------------------------------------------
# "Free-for-all" suite (bench_freeforall_cpp.cpp vs bench_freeforall_l.l).
# Same three workloads with every optimization on the table (reserved buffers,
# bump arenas, raw byte stores, libm). L side is still AOT -O3 native.
# ---------------------------------------------------------------------------
FF_CXX = BENCH_DIR.join("bench_freeforall_cpp.cpp")
FF_L = BENCH_DIR.join("bench_freeforall_l.l")

ff_cpp_exe = OUT_DIR.join("bench_freeforall_cpp.exe")
ff_l_exe = OUT_DIR.join("bench_freeforall_l.exe")

puts "\n[free-for-all] Compiling bench_freeforall_cpp.cpp with -O3 ..."
compile_cpp_o3!(FF_CXX, ff_cpp_exe)

puts "[free-for-all] Compiling bench_freeforall_l.l with -O3 ..."
compile_o3!([STDLIB_ARRAY, STDLIB_STRING, STDLIB_OPTIONAL, STDLIB_MAP,
             ROOT.join("stdlib", "thread.l"), ROOT.join("stdlib", "time.l"),
             FF_L], ff_l_exe)

ff_cpp_runs = []
ff_l_runs = []
3.times do |round|
  puts "\n[free-for-all] Round #{round + 1}"
  puts "--- C++ (bench_freeforall_cpp.exe) ---"
  _, out = time_exe!(ff_cpp_exe)
  puts out.to_s.gsub("\r\n", "\n")
  ff_cpp_runs << parse_ms(out)

  puts "--- L (bench_freeforall_l.exe) ---"
  _, out = time_exe!(ff_l_exe)
  puts out.to_s.gsub("\r\n", "\n")
  ff_l_runs << parse_ms(out)
end

ff_cpp_ms = %w[A B C].to_h { |k| [k, median(ff_cpp_runs.map { |r| r[k] }.compact)] }
ff_l_ms = %w[A B C].to_h { |k| [k, median(ff_l_runs.map { |r| r[k] }.compact)] }

puts "\n[free-for-all] median of 3"
puts "| test | C++ (ms) | L (ms) | L / C++ |"
puts "|------|---------:|-------:|--------:|"
%w[A B C].each do |k|
  c = ff_cpp_ms[k]
  l = ff_l_ms[k]
  next unless c && l
  # A dead-code-eliminated loop reports 0 ms; the ratio is meaningless, so say so.
  if c < 0.0005
    puts format("| %s | %.3f | %.3f | n/a (C++ loop elided) |", k, c, l)
    next
  end
  ratio = l / c
  puts format("| %s | %.3f | %.3f | %.2fx |", k, c, l, ratio)
end

puts "\nPASS"

