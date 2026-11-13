#!/usr/bin/env ruby
# frozen_string_literal: true

root = File.expand_path("..", __dir__)
out = File.join(root, "examples", "all_samples.l")

files = []
Dir.glob(File.join(root, "examples", "*.l")).sort.each do |f|
  next if File.basename(f) == "all_samples.l"
  files << f
end
Dir.glob(File.join(root, "tests", "cases", "**", "*.l")).sort.each do |f|
  next if File.basename(f).start_with?("_tmp")
  files << f
end

parts = files.map do |f|
  rel = f.sub("#{root}/", "").sub("#{root}\\", "").tr("\\", "/")
  body = File.read(f, encoding: "UTF-8")
  body = body.sub(/\A\uFEFF/, "").rstrip + "\n"
  "//------------------------------------------------\n// #{rel}\n//------------------------------------------------\n#{body}"
end

File.write(out, parts.join("\n"), encoding: "UTF-8")
puts "wrote #{out}"
puts "  #{files.size} files, #{File.size(out)} bytes"
