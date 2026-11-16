# Measure per-function relative brace nesting depth (read-only helper).
file = ARGV[0]
lines = File.readlines(file)

depth = 0
stack = []       # [{name:, start:, base:, max:}]
results = []

lines.each_with_index do |raw, idx|
  t = raw.strip
  opens = raw.count("{")
  closes = raw.count("}")

  # A function signature line is recognised when the current frame is a
  # namespace / class body (i.e. nothing is being measured) and the line
  # looks like `... name(args) {`.
  if stack.empty? && !t.empty? && t !~ %r{^(#|//)} && t.include?("(") && t.end_with?("{")
    unless t.start_with?("if ", "for ", "while ", "switch ", "} else", "else", "catch", "do ")
      stack << { name: t, start: idx + 1, base: depth, max: 0 }
    end
  end

  depth += opens - closes

  stack.each do |f|
    rel = depth - f[:base]
    f[:max] = rel if rel > f[:max]
  end

  while !stack.empty? && depth <= stack.last[:base]
    f = stack.pop
    results << [f[:max], f[:start], idx + 2 - f[:start], f[:name]]
  end
end

warn "functions parsed: #{results.size}"
warn "=== top 20 by max relative nesting ==="
results.sort_by { |r| -r[0] }.first(20).each do |r|
  warn format("  depth=%-3d L%-6d len=%-5d  %s", r[0], r[1], r[2], r[3][0, 92])
end
deep = results.select { |r| r[0] > 5 }.sort_by { |r| -r[0] }
warn "=== functions over depth 5: #{deep.size} of #{results.size} ==="
