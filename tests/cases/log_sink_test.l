// P1-6: log sinks (console / file / silent) and structured key-value output.
fun main() -> i32 {
    dir := String("build/clang-debug/test_out")
    create_dir(dir)
    log_path := path_join(dir, String("log_sink_test.log"))
    remove_file(log_path)

    // --- Console sink with filtering ---
    log_set_level(1)
    log_info(String("console_line"))
    log_debug(String("console_debug_hidden"))

    // --- Redirect to file ---
    r := log_to_file(log_path)
    if !r.is_ok() {
        print(String("fail: log_to_file"))
        print(r.unwrap_err())
        return 1
    }
    log_set_level(1)
    log_info(String("file_line_info"))
    log_error(String("file_line_error"))
    log_debug(String("file_debug_hidden"))

    // --- Structured key/value lines go to the same sink ---
    log_info_kv(String("cache"), String("hit"))
    log_warn_kv(String("retry"), String("3"))

    // --- Silent sink drops everything ---
    log_silence()
    log_error(String("silent_hidden"))

    // --- Back to console for the final marker ---
    log_to_console()
    log_set_level(1)

    // Verify file contents: entries present, filtered/silenced entries absent.
    opt := read_all(log_path)
    if !opt.is_some() {
        print(String("fail: log file not created"))
        return 1
    }
    content := opt.unwrap()
    if !content.contains(String("file_line_info")) {
        print(content)
        print(String("fail: info line missing from file"))
        return 1
    }
    if !content.contains(String("file_line_error")) {
        print(String("fail: error line missing from file"))
        return 1
    }
    if content.contains(String("file_debug_hidden")) {
        print(String("fail: debug leaked at level 1"))
        return 1
    }
    if content.contains(String("console_line")) {
        print(String("fail: console line leaked into file"))
        return 1
    }
    if content.contains(String("silent_hidden")) {
        print(String("fail: silenced line leaked into file"))
        return 1
    }
    if !content.contains(String("cache=hit")) {
        print(content)
        print(String("fail: structured kv missing"))
        return 1
    }
    if !content.contains(String("retry=3")) {
        print(String("fail: structured warn kv missing"))
        return 1
    }

    // --- A bad path must report, not silently swallow ---
    bad := log_to_file(String("Z:/definitely/not/here/x.log"))
    if bad.is_ok() {
        print(String("fail: bad sink path should error"))
        return 1
    }
    // Sink must stay usable after a failed switch.
    log_set_level(1)
    log_info(String("after_failed_switch"))

    remove_file(log_path)
    print(String("log_sink_ok"))
    return 0
}
