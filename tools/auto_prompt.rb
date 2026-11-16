#!/usr/bin/env ruby
# frozen_string_literal: true

# Minimal file-mailbox watcher.
# Polls for prompt.txt, prints its contents as "[EXEC] <content>", then deletes it.
# Any error is appended (with a timestamp) to prompt_error.log.
# Ctrl+C exits gracefully.

PROMPT_FILE = "prompt.txt"
ERROR_LOG   = "prompt_error.log"

$stdout.sync = true

begin
  loop do
    next sleep(0.5) unless File.exist?(PROMPT_FILE)

    content = File.read(PROMPT_FILE)
    File.delete(PROMPT_FILE)
    puts "[EXEC] #{content}"
  rescue Interrupt
    raise
  rescue => e
    message = "[#{Time.now.strftime('%Y-%m-%d %H:%M:%S')}] #{e.class}: #{e.message}\n"
    begin
      File.open(ERROR_LOG, "a") { |f| f.write(message) }
    rescue StandardError
      # Never let logging failures kill the watcher.
    end
    sleep(0.5)
  end
rescue Interrupt
  puts "\n[EXIT] watcher stopped."
end
