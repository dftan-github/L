#!/usr/bin/env ruby
# frozen_string_literal: true

# Whole-project consistency doctor.
#
# Answers one question: "I changed something — what else is now stale?"
#
# Three write tiers, strictly separated (this separation is the whole point):
#
#   CHECK  read-only. Never writes. A mismatch is reported and exits non-zero.
#   GEN    regenerable files whose content is derived from other files. Written
#          only with --write, and only ever pure regeneration (idempotent).
#   DOC    prose documents. With --write, only the monotonically-derivable
#          numbers inside them are corrected; wording is never touched.
#
# NEVER written, under any flag:
#   stdlib/**.l  tests/cases/*.l  compiler/**  runtime/*.c  .cursor/rules/*.mdc
# Comments-only files may be rewritten (examples/metaprogramming.l index block,
# examples/all_samples.l) because the edit cannot change program semantics.
#
# Usage:
#   ruby tools/doctor.rb              # report only, never write
#   ruby tools/doctor.rb --write      # also fix GEN + DOC items
#   ruby tools/doctor.rb --list       # list the checks and their tier
#   ruby tools/doctor.rb --quiet      # only show failures

require "pathname"

ROOT = Pathname.new(__dir__).parent
WRITE = ARGV.include?("--write")
LIST = ARGV.include?("--list")
QUIET = ARGV.include?("--quiet")

# Files the doctor must never open for writing, regardless of tier. Any check
# that resolves to one of these downgrades itself to CHECK.
IMMUTABLE = %w[
  .cursor/rules/language-rules.mdc
  compiler
  runtime
  stdlib
].freeze

def immutable?(rel)
  IMMUTABLE.any? { |p| rel == p || rel.start_with?("#{p}/") }
end

# ---------------------------------------------------------------- check model

Check = Struct.new(:id, :tier, :desc, :run, keyword_init: true)

CHECKS = []

def check(id, tier, desc, &blk)
  CHECKS << Check.new(id: id, tier: tier, desc: desc, run: blk)
end

$failures = 0
$fixed = 0

def fail!(msg)
  $failures += 1
  warn "  FAIL  #{msg}"
end

def ok!(msg)
  puts "  ok    #{msg}" unless QUIET
end

def fixed!(msg)
  $fixed += 1
  puts "  FIXED #{msg}"
end

# ------------------------------------------------------------------ the checks

# 1. Rule anchors: every <!-- id:xxx --> referenced in code exists in the rules
#    file. Cheap and authoritative — check_rule_refs.rb owns the parsing.
check("rule-anchors", :check, "rule anchors in code resolve against language-rules.mdc") do
  out = IO.popen(["ruby", ROOT.join("tools/check_rule_refs.rb").to_s], err: %i[child out], &:read)
  if $?.exitstatus.zero?
    ok!(out.strip)
  else
    fail!("rule anchors inconsistent\n#{out.gsub(/^/, '        ')}")
  end
end

# 2. examples/all_samples.l is generated from every sample. Comments only.
check("all-samples", :gen, "examples/all_samples.l matches its sources") do
  if WRITE
    out = IO.popen(["ruby", ROOT.join("tools/gen_all_samples.rb").to_s], &:read)
    if $?.exitstatus.zero?
      fixed!(out.lines.last(2).join(" ").strip)
    else
      fail!("gen_all_samples.rb errored: #{out.strip}")
    end
  else
    out = IO.popen(["ruby", ROOT.join("tools/gen_all_samples.rb").to_s, "--check"],
                   err: %i[child out], &:read)
    $?.exitstatus.zero? ? ok!(out.strip) : fail!(out.strip)
  end
end

# 3. examples/metaprogramming.l concept index — the row:N block at the top.
#    Comments only: the generator rewrites the header, never the code below.
check("meta-index", :gen, "metaprogramming.l row:N index matches real line numbers") do
  args = WRITE ? [] : ["--check"]
  out = IO.popen(["ruby", ROOT.join("tests/gen_meta_index.rb").to_s, *args],
                 err: %i[child out], &:read)
  if $?.exitstatus.zero?
    WRITE ? fixed!(out.strip) : ok!(out.strip)
  else
    fail!(out.strip)
  end
end

# 4. Line counts quoted in docs. Derived straight from the file, so a drift is
#    unambiguous. Only the number is corrected — prose is never rewritten.
DOC_ROW_RE = /\| \[`([^`]+)`\]\(([^)]+)\) \| (\d+) \|/.freeze

doc_targets = (Dir.glob(ROOT.join("docs", "*.md")) + [ROOT.join("README.md").to_s])
              .select { |f| File.file?(f) }

check("doc-line-counts", :doc, "line counts quoted in docs match the real files") do
  drift = 0
  doc_targets.each do |doc|
    rel_doc = Pathname.new(doc).relative_path_from(ROOT).to_s.tr("\\", "/")
    next if immutable?(rel_doc)
    text = File.read(doc, encoding: "UTF-8")
    updated = text.dup
    text.scan(DOC_ROW_RE) do |_name, rel, claimed|
      target = Pathname.new(doc).dirname.join(rel).cleanpath
      next unless target.file?
      real = target.read(encoding: "UTF-8").lines.length
      next if real == claimed.to_i
      drift += 1
      if WRITE
        updated.sub!(/\| \[`#{Regexp.escape(_name)}`\]\(#{Regexp.escape(rel)}\) \| #{claimed} \|/,
                     "| [`#{_name}`](#{rel}) | #{real} |")
      else
        warn "  FAIL  #{rel_doc}: #{rel} claims #{claimed} lines, real #{real}"
      end
    end
    next if WRITE == false || updated == text
    File.write(doc, updated, encoding: "UTF-8")
    fixed!("#{rel_doc}: corrected #{drift} line count(s)")
    drift = 0
  end
  if WRITE
    ok!("doc line counts up to date") if drift.zero?
  elsif drift.zero?
    ok!("#{doc_targets.length} docs agree with their files")
  else
    fail!("#{drift} stale line count(s) in docs (fix with --write)")
  end
end

# 5. Trailing-underscore identifiers in .l sources. Semantics would change, so
#    this is CHECK-only by construction — it can never be auto-fixed here.
#
# String literals are stripped first: LCMD handlers legitimately build code
# fragments like "i32 gen_" + args + " = 1" where the underscore sits inside a
# string, and the resulting identifier is emitted as `gen_<n>`.
UNDERSCORE_RE = /(?<![\w.])[A-Za-z][A-Za-z0-9]*_(?![A-Za-z0-9_])/
UNDERSCORE_ALLOWED = %w[__FILE__ __LINE__ __FUNC__].freeze

def strip_l_strings(line)
  out = +""
  i = 0
  in_str = false
  while i < line.length
    ch = line[i]
    if in_str
      if ch == "\\"
        i += 2
        next
      end
      in_str = false if ch == '"'
    else
      if ch == '"'
        in_str = true
      elsif ch == "/" && line[i + 1] == "/"
        break
      else
        out << ch
      end
    end
    i += 1
  end
  out
end

check("no-trailing-underscore", :check, "L sources contain no trailing-underscore identifiers") do
  hits = []
  %w[stdlib tests/cases examples].each do |dir|
    Dir.glob(ROOT.join(dir, "**", "*.l")).sort.each do |f|
      rel = Pathname.new(f).relative_path_from(ROOT).to_s.tr("\\", "/")
      File.readlines(f, encoding: "UTF-8").each_with_index do |line, i|
        code = strip_l_strings(line)
        code.scan(UNDERSCORE_RE) do |m|
          next if UNDERSCORE_ALLOWED.include?(m)
          hits << "#{rel}:#{i + 1}: #{m}"
        end
      end
    end
  end
  if hits.empty?
    ok!("no trailing-underscore identifiers")
  else
    fail!("#{hits.length} trailing-underscore identifier(s):\n#{hits.first(10).map { |h| "        #{h}" }.join("\n")}")
  end
end

# 6. Cross-file row:N links written in comments. Pure existence check.
ROW_LINK_RE = %r{(?:^|\s)([\w./-]+\.l)\s+row:(\d+)}.freeze

check("row-links", :check, "cross-file row:N links point at existing files and lines") do
  bad = []
  globs = %w[examples/*.l docs/*.md README.md]
  globs.flat_map { |g| Dir.glob(ROOT.join(g)) }.sort.each do |f|
    rel = Pathname.new(f).relative_path_from(ROOT).to_s.tr("\\", "/")
    # all_samples.l is a concatenation: its row:N anchors index lines of the
    # GENERATED file itself, not the linked sample. Its correctness is owned by
    # the all-samples check above, so skip it here to avoid false positives.
    next if rel == "examples/all_samples.l"
    File.readlines(f, encoding: "UTF-8").each_with_index do |line, i|
      line.scan(ROW_LINK_RE) do |path, num|
        # Anchors may be written relative to the containing file (hand-written
        # docs) or relative to the repo root (generated TOCs). Accept either.
        candidates = [
          Pathname.new(f).dirname.join(path).cleanpath,
          ROOT.join(path).cleanpath
        ].uniq
        nxt = candidates.find(&:file?)
        if nxt.nil?
          bad << "#{rel}:#{i + 1}: missing file #{path}"
        elsif num.to_i > nxt.read(encoding: "UTF-8").lines.length
          bad << "#{rel}:#{i + 1}: #{path} has no row #{num}"
        end
      end
    end
  end
  bad.empty? ? ok!("all row:N links resolve") : fail!(bad.map { |b| "  #{b}" }.join("\n"))
end

# ----------------------------------------------------------------------- main

if LIST
  puts format("%-24s %-6s %s", "CHECK", "TIER", "DESCRIPTION")
  puts "-" * 78
  CHECKS.each { |c| puts format("%-24s %-6s %s", c.id, c.tier.to_s.upcase, c.desc) }
  puts
  puts "Tiers: CHECK = read-only, GEN = pure regeneration, DOC = numbers only."
  puts "Code, rules and .mdc files are never written. --write enables GEN + DOC."
  exit 0
end

puts "L doctor — #{WRITE ? 'REPORT + WRITE' : 'REPORT ONLY (add --write to fix GEN/DOC)'}"
puts "=" * 78
CHECKS.each do |c|
  puts "[#{c.tier.to_s.upcase}] #{c.id} — #{c.desc}"
  c.run.call
  puts
end

puts "=" * 78
if $failures.zero?
  puts $fixed.positive? ? "PASS (#{$fixed} item(s) fixed)" : "PASS — everything consistent"
  exit 0
end

puts "FAIL — #{$failures} problem(s)#{$fixed.positive? ? ", #{$fixed} fixed" : ''}"
puts "Run with --write to fix GEN/DOC tiers; CHECK tiers are manual." if WRITE == false
exit 1
