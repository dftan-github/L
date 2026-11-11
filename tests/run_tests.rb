#!/usr/bin/env ruby
# frozen_string_literal: true

# L compiler test runner (Ruby only for automation).
# Walks tests/cases/*.l, invokes the compiler, compares stdout/stderr to tests/expected/.

require "fileutils"
require "open3"
require "pathname"

ROOT = Pathname.new(__dir__).parent.expand_path
CASES_DIR = ROOT.join("tests", "cases")
EXPECTED_DIR = ROOT.join("tests", "expected")
# Prefer build/clang-debug once the driver target exists.
COMPILER = ENV.fetch("L_COMPILER", ROOT.join("build", "clang-debug", "l_compiler_driver.exe").to_s)

def fail!(msg)
  warn "FAIL: #{msg}"
  exit 1
end

def run_case(path)
  name = path.basename(".l").to_s
  expected_out = EXPECTED_DIR.join("#{name}.txt")
  expected_err = EXPECTED_DIR.join("#{name}.err")

  unless File.exist?(COMPILER)
    warn "SKIP #{name}: compiler not built yet (#{COMPILER})"
    return :skip
  end

  stdout, stderr, status = Open3.capture3(COMPILER, path.to_s)

  if expected_err.exist?
    body = expected_err.read
    fail!("#{name}: expected compile error matching #{expected_err}") unless stderr.include?(body.strip)
    puts "OK   #{name} (error)"
    return :ok
  end

  fail!("#{name}: compiler exited #{status.exitstatus}\n#{stderr}") unless status.success?

  if expected_out.exist?
    got = stdout
    want = expected_out.read
    fail!("#{name}: stdout mismatch\n--- got ---\n#{got}\n--- want ---\n#{want}") unless got == want
  end

  puts "OK   #{name}"
  :ok
end

def main
  FileUtils.mkdir_p(EXPECTED_DIR)
  cases = Dir.glob(CASES_DIR.join("**", "*.l").to_s).sort
  fail!("no .l cases under #{CASES_DIR}") if cases.empty?

  results = cases.map { |p| run_case(Pathname.new(p)) }
  skipped = results.count(:skip)
  puts "done: #{results.count(:ok)} ok, #{skipped} skipped"
  exit 0
end

main
