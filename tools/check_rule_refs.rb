#!/usr/bin/env ruby
# Rule-anchor reference guard.
#
# The rules file (.cursor/rules/language-rules.mdc) numbers sections 1..N plus
# 999. Numbers shift whenever sections are added or removed, so source comments
# must reference the stable anchor id (<!-- id:delegate -->) rather than "rule 97".
#
# This script fails (exit 1) when:
#   1. a "## N. Title" heading has no <!-- id:xxx --> anchor right below it
#   2. an anchor id is duplicated
#   3. section numbers are not unique
#   4. code references an anchor id (see --list) that is not declared in the rules file
#   5. code still contains a numeric reference like "rule 97" / "Rule 97"
#
# Usage:
#   ruby tools/check_rule_refs.rb            # check everything
#   ruby tools/check_rule_refs.rb --list     # also print the anchor table
require "pathname"

ROOT = Pathname.new(__dir__).parent
RULES = ROOT.join(".cursor", "rules", "language-rules.mdc")
CODE_GLOBS = %w[
  compiler/**/*.cpp
  compiler/**/*.hpp
  runtime/*.c
  tools/*.rb
  tests/*.rb
].freeze

ANCHOR_RE = /^<!-- id:([a-z0-9][a-z0-9-]*) -->$/
HEADING_RE = /^## (\d+)\. (.+)$/
REF_RE = /\[([a-z][a-z0-9]*(?:-[a-z0-9]+)*)\]/
NUMERIC_RE = /\b[Rr]ules?\s+\d+\b/

# Words that appear in brackets in prose/indexing and must never be read as anchors.
PLAIN_WORDS = %w[
  key keys value values name names path paths file files index idx cache
  this self argv args type types flag flags tag tags node nodes
  item items part parts size len mode data text line lines
  test tests case cases true false null none main index name
].freeze

errors = []

unless RULES.file?
  warn "rules file not found: #{RULES}"
  exit 2
end

lines = RULES.read.force_encoding("UTF-8").split("\n", -1)
anchors = {}      # id => section number
sections = {}     # number => id
table = []

lines.each_with_index do |line, i|
  heading = HEADING_RE.match(line)
  next unless heading

  num = heading[1].to_i
  title = heading[2].strip
  anchor_line = lines[i + 1].to_s
  match = ANCHOR_RE.match(anchor_line.strip)

  if match.nil?
    errors << "line #{i + 1}: section #{num} (#{title}) has no anchor line below it"
    next
  end

  id = match[1]
  errors << "line #{i + 1}: duplicate section number #{num}" if sections.key?(num)
  errors << "line #{i + 1}: duplicate anchor id '#{id}' (also used by section #{anchors[id]})" if anchors.key?(id)

  anchors[id] = num
  sections[num] = id
  table << [num, id, title]
end

# Section numbers must not repeat; gaps are reported separately (informational).
errors << "no sections found in #{RULES.basename}" if sections.empty?

CODE_GLOBS.flat_map { |g| Dir.glob(ROOT.join(g).to_s) }.uniq.sort.each do |file|
  rel = Pathname.new(file).relative_path_from(ROOT).to_s.tr("\\", "/")
  File.readlines(file, encoding: "UTF-8").each_with_index do |line, i|
    # Strip string literals and C++/Ruby comments' contents so doc text like
    # "[ERROR] file:line:col" or "[package]" is not mistaken for a reference.
    scan = line.dup
    scan.gsub!(/"[^"]*"/, '""')
    scan.gsub!(/'[^']*'/, "''")

    comment_body =
      case rel
      when /\.(cpp|hpp|c)$/ then scan[/\/\/(.*)$/, 1] || scan[/\/\*(.*?)(\*\/|\z)/, 1]
      when /\.rb$/ then scan[/#(.*)$/, 1]
      end
    next if comment_body.nil?

    if comment_body =~ NUMERIC_RE
      errors << "#{rel}:#{i + 1}: numeric rule reference — use an anchor id instead: #{line.strip}"
    end

    comment_body.scan(REF_RE) do |(id)|
      # Only flag ids that a human could plausibly have meant as an anchor.
      # Skip: short words, plain English, Ruby/C indexing like cache["files"][key],
      # TOML table names ([package], [dependencies]) and diagnostic tags.
      next if id.length < 4
      next if %w[error warning package dependencies].include?(id)
      next if PLAIN_WORDS.include?(id)
      errors << "#{rel}:#{i + 1}: unknown anchor '#{id}' (not declared in #{RULES.basename})" unless anchors.key?(id)
    end
  end
end

if ARGV.include?("--list")
  table.sort_by(&:first).each { |num, id, title| puts format("%4d  %-28s %s", num, id, title) }
  puts
end

if errors.empty?
  puts "OK: #{anchors.size} anchors, all references resolve."
  exit 0
end

warn "FAIL: #{errors.size} rule-reference problem(s)"
errors.each { |e| warn "  - #{e}" }
exit 1
