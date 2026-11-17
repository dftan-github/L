#!/usr/bin/env ruby
# Rule-section anchor guard for .cursor/rules/language-rules.mdc
#
# Every numbered section must carry its anchor on the line directly below the
# heading:
#
#     ## 97. Implicit Delegation (Struct Embedding)
#     <!-- anchor:97-Implicit-Delegation -->
#
# Only the NUMBER is authoritative. The label after the dash is human text and
# may be rewritten freely; nothing resolves on it. Numbers are APPEND-ONLY: a
# section keeps its number forever, and a new section takes max+1, so inserting
# a rule never renumbers — and never breaks — an existing reference.
#
# Fails (exit 1) when:
#   1. a numbered heading has no anchor line below it, or the line is malformed
#   2. a section number or anchor number is duplicated
#   3. an anchor number does not match its section number (drift after an edit)
#   4. code references "rule 97" numerically instead of by anchor
#
# Usage:
#   ruby tools/check_rule_refs.rb            # verify
#   ruby tools/check_rule_refs.rb --list     # also print the numbering table

require "pathname"

ROOT = Pathname.new(__dir__).parent
RULES = ROOT.join(".cursor", "rules", "language-rules.mdc")

CODE_GLOBS = %w[
  compiler/**/*.cpp
  compiler/**/*.hpp
  runtime/*.c
  CMakeLists.txt
  runtime/CMakeLists.txt
  tools/*.rb
  tests/*.rb
].freeze

# Section numbers may be flat (99), multi-level (999.1) or suffixed (12a).
HEADING_RE = %r{^## (\d+(?:\.\d+)*[a-z]?)\.?\s+(.+)$}
# <!-- anchor:97-Implicit-Delegation -->
ANCHOR_RE = %r{^<!-- anchor:(\d+)-([^\s>]+) -->$}
NUMERIC_RE = /\b[Rr]ules?\s+\d+\b/

errors = []

unless RULES.file?
  warn "rules file not found: #{RULES}"
  exit 2
end

lines = RULES.read(encoding: "UTF-8").split("\n", -1)
by_section = {}   # section number  => anchor number
by_id      = {}   # anchor number   => section number
table      = []
headings   = 0

lines.each_with_index do |line, i|
  # A heading that starts with a digit but does not parse is a hard error:
  # skipping it silently is how an un-anchored section slips through.
  if line =~ %r{^##\s+\d} && HEADING_RE.match(line).nil?
    errors << "line #{i + 1}: unrecognized numbered heading " \
              "(expected '## <number>. <title>'): #{line.strip}"
    next
  end

  heading = HEADING_RE.match(line)
  next unless heading

  headings += 1
  num = heading[1]
  title = heading[2].strip
  raw = lines[i + 1].to_s.strip
  match = ANCHOR_RE.match(raw)

  if match.nil?
    errors << "line #{i + 1}: section #{num} (#{title}) has no " \
              "'<!-- anchor:N-Label -->' line below it"
    next
  end

  id = match[1]
  label = match[2]

  # Anchor numbers are allocated in document order (1..N) and are deliberately
  # NOT forced to equal the section number: sections such as 999 live in a
  # separate namespace, and renumbering a section must never move its anchor.
  if by_section.key?(num)
    errors << "line #{i + 1}: duplicate section number #{num}"
  end
  if by_id.key?(id)
    errors << "line #{i + 1}: duplicate anchor number #{id} " \
              "(already used by section #{by_id[id]})"
  end
  errors << "line #{i + 1}: anchor #{id} has an empty label" if label.empty?

  by_section[num] = id
  by_id[id] = num
  table << [num, id, label, title]
end

errors << "no sections found in #{RULES.basename}" if by_section.empty?
errors << "every heading lost its anchor (0 verified)" if headings.positive? && by_section.empty?

# Numeric rule references are banned: they rot the moment a rule is inserted.
CODE_GLOBS.flat_map { |g| Dir.glob(ROOT.join(g).to_s) }.uniq.sort.each do |file|
  rel = Pathname.new(file).relative_path_from(ROOT).to_s.tr("\\", "/")
  File.readlines(file, encoding: "UTF-8").each_with_index do |line, i|
    scan = line.dup
    scan.gsub!(/"[^"]*"/, '""')
    scan.gsub!(/'[^']*'/, "''")
    body =
      case rel
      when /\.(cpp|hpp|c)$/ then scan[%r{//(.*)$}, 1] || scan[%r{/\*(.*?)(\*/|\z)}, 1]
      when /\.rb$/, %r{(^|/)CMakeLists\.txt$} then scan[/#(.*)$/, 1]
      end
    next if body.nil?
    next unless body =~ NUMERIC_RE
    errors << "#{rel}:#{i + 1}: numeric rule reference — link by anchor instead: #{line.strip}"
  end
end

if ARGV.include?("--list")
  sort_key = lambda do |(num, _id, _label, _title)|
    parts = num.scan(/\d+/).map(&:to_i)
    parts.fill(0, parts.size...3)
    parts
  end
  table.sort_by(&sort_key).each do |num, id, label, title|
    puts format("%7s  %-4s %-40s %s", num, id, label, title[0, 44])
  end
  puts
end

if errors.empty?
  puts "OK: #{by_section.size} rule anchors, all references resolve " \
       "(numbers match sections, no numeric refs)."
  exit 0
end

warn "FAIL: #{errors.size} rule-anchor problem(s)"
errors.each { |e| warn "  - #{e}" }
exit 1
