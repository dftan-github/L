#!/usr/bin/env ruby
# frozen_string_literal: true

# Anchor id allocator.
#
# IDs ARE NUMBERS, AND ONLY NUMBERS RESOLVE
#   A definition is  [anchor:97-Implicit-Delegation]
#   A reference is   [link:97-Implicit-Delegation]
#   The digits are the identity; the label after the dash is human text and may
#   be rewritten at will. Rule sections carry the same marker as a bare HTML
#   comment: <!-- anchor:97-Implicit-Delegation -->
#
# WHY A COUNTER, NOT A TITLE-DERIVED SLUG
#   A slug changes when a title is edited, which silently moves every reference.
#   A number never changes. .anchors.json keeps the single fact a number cannot
#   encode: which numbers have already been handed out, so a retired id is never
#   reissued to an unrelated anchor.
#
# Usage:
#   ruby tools/anchor.rb new "In-Place Class-Body LCMD"
#       -> 137                    (the next free id; counter advanced if --write)
#
#   ruby tools/anchor.rb new "Title" --write
#       -> prints the ready-to-paste marker AND advances the counter
#
#   ruby tools/anchor.rb mark "Title"      # print the paste-ready marker
#   ruby tools/anchor.rb mark "Title" --l  # print the source-comment form
#   ruby tools/anchor.rb list              # every anchor in use
#   ruby tools/anchor.rb find 137          # what owns this id?
#   ruby tools/anchor.rb check             # verify counter vs. ids on disk

require "json"
require "pathname"

ROOT = Pathname.new(__dir__).parent
REGISTRY = ROOT.join(".anchors.json")
RULES = ROOT.join(".cursor", "rules", "language-rules.mdc")

SCAN_GLOBS = %w[
  .cursor/rules/*.mdc
  examples/**/*.l
  tests/cases/**/*.l
  stdlib/**/*.l
  compiler/**/*.cpp
  compiler/**/*.hpp
  runtime/*.c
  CMakeLists.txt
  runtime/CMakeLists.txt
].freeze

SKIP_GEN = "all_samples.l"

# <-- anchor:N-Label -->   and   [anchor:N-Label]
DEF_RE = %r{(?:<!--\s*anchor:|\[anchor:)(\d+)-([^\s>\]]*)}.freeze

STOP_WORDS = %w[
  a an the of for in on at to and or with without by from into over under
  is are be as it its this that these those vs no
].freeze

def label_for(title)
  s = title.dup
  s.gsub!(/`([^`]*)`/, '\1')
  s.gsub!(/\([^)]*\)/, " ")
  s.gsub!(%r{[^\w\s-]}, " ")
  words = s.split(/[\s_-]+/).reject(&:empty?)
  words.reject! { |w| STOP_WORDS.include?(w.downcase) }
  words.first(5).map { |w| w[0].upcase + w[1..].to_s }.join("-")
end

# id => { label:, where: }
def definitions
  out = {}
  SCAN_GLOBS.flat_map { |g| Dir.glob(ROOT.join(g)) }.uniq.sort.each do |file|
    next if File.basename(file) == SKIP_GEN
    rel = Pathname.new(file).relative_path_from(ROOT).to_s.tr("\\", "/")
    File.readlines(file, encoding: "UTF-8").each_with_index do |line, i|
      line.scan(DEF_RE) do |(id, label)|
        out[id] ||= { label: label, where: "#{rel}:#{i + 1}", count: 0 }
        out[id][:count] += 1
      end
    end
  end
  out
end

def read_counter
  return nil unless REGISTRY.exist?
  JSON.parse(REGISTRY.read(encoding: "UTF-8"))["next_anchor_id"].to_i
end

def write_counter(value)
  REGISTRY.write(JSON.generate({ "next_anchor_id" => value }) + "\n", encoding: "UTF-8")
end

# Atomic allocate. Reading, computing and writing the counter must happen under
# one exclusive lock: two callers (an editor command and an agent) can otherwise
# read the same value and both be handed the same id. The lock file is a sibling,
# so the registry itself stays a single clean line.
LOCK_PATH = ROOT.join(".anchors.lock")

# Serialises the whole read-compute-write cycle, returning the id reserved LAST.
def with_registry_lock
  File.open(LOCK_PATH, File::RDWR | File::CREAT, 0o644) do |f|
    f.flock(File::LOCK_EX)
    begin
      yield
    ensure
      f.flock(File::LOCK_UN)
    end
  end
end

# Reserve `count` consecutive ids atomically and advance the counter once.
def reserve(count)
  with_registry_lock do
    defs = definitions
    base = next_id(defs, read_counter)
    write_counter(base + count)
    Array.new(count) { |i| base + i }
  end
end

# The next id must clear both the counter and every id currently on disk; the
# counter is the safety net for ids that were used and later deleted.
def next_id(defs, counter)
  [counter.to_i, defs.keys.map(&:to_i).max.to_i + 1].max
end

cmd = ARGV[0]
marker_html = ->(id, label) { "<!-- anchor:#{id}-#{label} -->" }
marker_src  = ->(id, label) { "// [anchor:#{id}-#{label}]" }

case cmd
when "new"
  title = ARGV[1]
  abort 'usage: ruby tools/anchor.rb new "Section Title" [--write]' if title.nil? || title.strip.empty?
  label = label_for(title)
  abort "cannot derive a label from #{title.inspect}" if label.empty?

  id =
    if ARGV.include?("--write")
      reserve(1).first
    else
      # Read-only preview: no reservation, so nothing is consumed.
      preview = next_id(definitions, read_counter)
      puts preview
      puts marker_html.call(preview, label)
      warn "  counter NOT advanced — rerun with --write to reserve the id"
      exit 0
    end

  puts id
  puts marker_html.call(id, label)

when "mark"
  title = ARGV[1]
  abort 'usage: ruby tools/anchor.rb mark "Section Title" [--l]' if title.nil? || title.strip.empty?
  label = label_for(title)
  defs = definitions
  id = next_id(defs, read_counter)

  if ARGV.include?("--l")
    puts marker_src.call(id, label)
  else
    puts marker_html.call(id, label)
  end
  warn "  id #{id} is free; run 'new ... --write' to reserve it" unless ARGV.include?("--quiet")

when "list"
  defs = definitions
  defs.sort_by { |id, _| id.to_i }.each do |id, m|
    warn format("%5s  %-38s %s%s", id, m[:label], m[:where],
                m[:count] > 1 ? "  DUPLICATE x#{m[:count]}" : "")
  end
  puts "#{defs.size} anchors in use; next_anchor_id=#{read_counter || '?'}"

when "find"
  q = ARGV[1].to_s
  abort "usage: ruby tools/anchor.rb find <id-or-label>" if q.empty?
  defs = definitions
  hits = defs.select { |id, m| id == q || m[:label].downcase.include?(q.downcase) }
  if hits.empty?
    warn "no anchor matches #{q.inspect}"
    exit 1
  end
  hits.each { |id, m| puts format("%5s  %-38s %s", id, m[:label], m[:where]) }

when "check"
  defs = definitions
  counter = read_counter
  if counter.nil?
    warn "FAIL: .anchors.json missing or has no next_anchor_id"
    exit 1
  end
  max_id = defs.keys.map(&:to_i).max.to_i
  if counter <= max_id
    warn "FAIL: next_anchor_id=#{counter} does not exceed the highest id in use (#{max_id})"
    exit 1
  end
  dups = defs.select { |_, m| m[:count] > 1 }
  unless dups.empty?
    dups.each { |id, m| warn "FAIL: anchor #{id} defined #{m[:count]}x (#{m[:where]})" }
    exit 1
  end
  puts "OK: #{defs.size} anchors, next_anchor_id=#{counter}"

else
  puts <<~TXT
    Anchor id allocator (numeric ids; only the number resolves).

      ruby tools/anchor.rb new "Section Title"          propose the next id
      ruby tools/anchor.rb new "Section Title" --write  reserve it (advances counter)
      ruby tools/anchor.rb mark "Section Title" [--l]   paste-ready marker only
      ruby tools/anchor.rb list                         every anchor in use
      ruby tools/anchor.rb find 137                     who owns this id
      ruby tools/anchor.rb check                        counter vs. ids on disk

    Labels are derived from the title for readability only; links match the digits.
    After inserting, run: ruby tests/check_anchors.rb
  TXT
  exit(cmd.nil? ? 0 : 1)
end
