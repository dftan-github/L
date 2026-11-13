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
puts "PASS"
