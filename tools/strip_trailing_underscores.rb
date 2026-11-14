#!/usr/bin/env ruby
# Strip trailing `_` from L identifiers outside strings/comments ([no-trailing-underscore] migration).
require "pathname"

root = Pathname.new(__dir__).parent
files = %w[stdlib tests/cases examples].flat_map { |d| Pathname.glob(root.join(d, "**", "*.l")) }
changed = []

files.each do |path|
  src = path.read
  out = +""
  i = 0
  while i < src.length
    if src[i] == "/" && src[i + 1] == "/"
      while i < src.length && src[i] != "\n"
        out << src[i]
        i += 1
      end
      next
    end
    if src[i] == "\""
      out << src[i]
      i += 1
      while i < src.length
        if src[i] == "\\" && i + 1 < src.length
          out << src[i]
          out << src[i + 1]
          i += 2
          next
        end
        out << src[i]
        break if src[i] == "\""
        i += 1
      end
      i += 1
      next
    end
    if src[i].match?(/[A-Za-z_]/)
      j = i + 1
      j += 1 while j < src.length && src[j].match?(/[A-Za-z0-9_]/)
      ident = src[i...j]
      if ident.end_with?("_") && ident != "_"
        out << ident[0...-1]
      else
        out << ident
      end
      i = j
      next
    end
    out << src[i]
    i += 1
  end
  next if out == src
  path.write(out)
  changed << path.relative_path_from(root).to_s
end

puts "changed #{changed.size} files"
changed.each { |f| puts f }
