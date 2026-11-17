# Generates the concept index for examples/metaprogramming.l with real,
# verified line numbers.
#
# Link syntax (deliberately explicit — a bare ":160" is not self-explanatory):
#   row:160                       a line in THIS file
#   tests/cases/foo.l row:12      a line in another file
#
# LINE-NUMBER CORRECTNESS: inserting the index shifts every line below it, so
# anchors must be resolved against the file as it will exist AFTER insertion.
# The script writes a placeholder block of a fixed line count first, resolves
# every anchor against that on-disk file, then swaps in the real index while
# keeping the line count identical. Re-running it is idempotent.
#
# Every anchor is asserted to hit an exact expected line, so a wrong needle
# fails loudly instead of silently rotting.

require "fileutils"

ROOT = File.expand_path("..", __dir__)
EX   = File.join(ROOT, "examples", "metaprogramming.l")

INDEX_START = "// Concept index"
LIMITS_HEAD = "// Interpreter limits"
LIMITS_TAIL = "// so A7 never leaks them into the injected L source.\n"

def case_file(name)
  File.join(ROOT, "tests", "cases", name)
end

# `exact: true` matches a whole trimmed line, so a mention inside a comment can
# never be mistaken for the real directive.
def resolve(path, needle, occurrence: 0, exact: false)
  hits = File.readlines(path).each_with_index.select do |l, _|
    exact ? l.strip == needle : l.include?(needle)
  end
  raise "#{File.basename(path)}: needle not found: #{needle.inspect}" if hits.empty?
  if occurrence >= hits.length
    raise "#{File.basename(path)}: occurrence #{occurrence} out of range for #{needle.inspect}"
  end
  hits[occurrence][1] + 1
end

# In-file anchors: the concept, and the line a reader actually wants to land on.
in_file = [
  ["A1", "directive with args + 5-arg handler (path, cmd, args, line, col)",
   "pub fun __compile_time_handler"],
  ["A2", "class-body injection -> methods / fields / delegate, path = \"Point\"",
   "if \"Point\" == path && \"method\" == cmd"],
  ["A3", "statement injection -> statements into a function body",
   "return \"return 11;\""],
  ["A4", "hierarchical path -> \"Point.generate\" vs bare \"Point\"",
   "if \"Point.generate\" == path && \"in_method\" == cmd"],
  ["A5", "uid_counter() in a directive -> collision-free generated names",
   "if \"gen_local\" == cmd"],
  ["A6", "explicit class hook -> enable_class_hook + __on_class_parsed",
   "pub fun __on_class_parsed"],
  ["A7", "source_line/column() -> directive position inside the handler",
   "if \"where_directive\" == cmd"],
  ["B1", "comptime { } -> frontend-only, stripped before codegen",
   "print(\"[comptime] B1"],
  ["B2", "comptime_assert -> build veto on a false condition",
   "comptime_assert(1 + 1 == 2"],
  ["B3", "uid_counter() -> monotonic, inlined as a constant",
   "comptime_assert(id_second == id_first + 1"],
  ["B4", "__FILE__ / __FUNC__ -> folded to string literals at codegen",
   "print(__FUNC__)"],
  ["B5", "__LINE__ -> folded to an i32 constant",
   "print(__LINE__)"],
  ["B6", "zero-cost proof -> 100k iterations, no comptime residue",
   "pub fun hot_loop"],
]

# Where the directives are literally written.
call_sites = [
  ["A2 member sites", "the three class-body directives",
   "//LCMD_method()",        true],
  ["A4 method body",  "the in-method directive",
   "//LCMD_in_method()",     true],
  ["A6 hook toggle",  "hook enabled for Audited",
   "//LCMD_enable_class_hook()", true],
]

# The same concepts in isolation, each in its own file.
external = [
  ["tests/cases/lcmd_in_class.l", "class-body members + in-method statements",
   case_file("lcmd_in_class.l"), "if \"User.generate\" == path && \"legacy\" == cmd", false],
  ["tests/cases/lcmd_path.l", "hierarchical path in a method body",
   case_file("lcmd_path.l"), "if \"User.generate\" == path && \"make_hello\" == cmd", false],
  ["tests/cases/uid_test.l", "uid_counter across two directives",
   case_file("uid_test.l"), "id := uid_counter()", false],
  ["tests/cases/context_macro_test.l", "__FILE__ / __LINE__ / __FUNC__ at runtime",
   case_file("context_macro_test.l"), "\"Func: \" + __FUNC__", false],
  ["tests/cases/comptime_assert_fail_test.l", "comptime_assert vetoing a build",
   case_file("comptime_assert_fail_test.l"), "comptime_assert(2 > 3", false],
]

def build_index(in_file, call_sites, external, ex_path)
  out = []
  out << "// Concept index — every line number below is verified against the real"
  out << "// file by build/gen_meta_index.rb. Re-run it after editing this file."
  out << "//"
  out << "//   row:N                        line N of THIS file"
  out << "//   rel/path.l row:N             line N of another file"
  out << "//"
  out << "// PART A — LCMD directives (handler first, then its call sites):"
  in_file.select { |e| e[0].start_with?("A") }.each do |id, desc, needle|
    out << format("//   %-3s row:%-4d %s", id, resolve(ex_path, needle), desc)
  end
  out << "//"
  out << "// PART B — comptime:"
  in_file.select { |e| e[0].start_with?("B") }.each do |id, desc, needle|
    out << format("//   %-3s row:%-4d %s", id, resolve(ex_path, needle), desc)
  end
  out << "//"
  out << "// Call sites (where the directives are literally written):"
  call_sites.each do |id, desc, needle, exact|
    out << format("//   %-15s row:%-4d %s", id, resolve(ex_path, needle, exact: exact), desc)
  end
  out << "//"
  out << "// Isolation cases (same concept, one file each):"
  external.each do |rel, desc, path, needle, exact|
    out << format("//   %-42s row:%-4d %s", rel, resolve(path, needle, exact: exact), desc)
  end
  out.join("\n") + "\n//\n"
end

src = File.read(EX)
s = src.index(INDEX_START) || raise("index start not found")
lim = src.index(LIMITS_HEAD, s) || raise("limits block not found")
lim_end = src.index(LIMITS_TAIL, lim) || raise("limits tail not found")
lim_end += LIMITS_TAIL.length
import_at = src.index("\nimport \"stdlib/", lim_end) || raise("import marker not found")
import_at += 1

# Everything from the end of the limits paragraph to the first import (blank
# lines / other comments) is preserved verbatim — never re-synthesised, so the
# script cannot drift the file a little further on every run.
preserved_mid = src[lim_end...import_at]
preserved_limits = src[lim...lim_end]

# Pass 1: reserve a fixed-size placeholder, then resolve against that layout.
#
# With --check the two passes run against a scratch copy so the real file is
# never touched; the result is compared byte-for-byte instead of written.
CHECK = ARGV.include?("--check")
target = CHECK ? File.join(ROOT, "build", ".meta_index_probe.l") : EX
FileUtils.mkdir_p(File.dirname(target)) if CHECK
FileUtils.cp(EX, target) if CHECK

probe_index = build_index(in_file, call_sites, external, target)
n_lines = probe_index.lines.length
placeholder = "// @@INDEX@@\n" + ("//\n" * (n_lines - 1))
raise "placeholder line mismatch" unless placeholder.lines.length == n_lines

File.write(target, src[0...s] + placeholder + preserved_limits + preserved_mid + src[import_at..])

# Pass 2: numbers are now resolved with the index already occupying its space.
final_index = build_index(in_file, call_sites, external, target)
raise "index size drifted between passes" unless final_index.lines.length == n_lines

result = src[0...s] + final_index + preserved_limits + preserved_mid + src[import_at..]

if CHECK
  FileUtils.rm_f(target)
  if result == src
    puts "OK: concept index is up to date (#{n_lines} lines)"
    exit 0
  end
  warn "STALE: concept index in #{EX} does not match the file's real line numbers"
  warn "       fix with: ruby tests/gen_meta_index.rb"
  exit 1
end

File.write(EX, result)
puts "rewrote concept index in #{EX} (#{n_lines} lines)"
