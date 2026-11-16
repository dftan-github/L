// Structured logging with a global level filter and selectable sink.
//
// Levels: 0 = DEBUG, 1 = INFO, 2 = WARN, 3 = ERROR, 4 = OFF.
// Sinks: 0 = console (stdout), 1 = file (appended), 2 = silent.
//
// Design notes:
// - Level is checked before any formatting, so a suppressed call costs one compare.
// - The sink is a module global; `log_to_file` opens it once and appends per line.
// - [performance-first] No allocation happens on the filtered-out path.
import "stdlib/file.l"
import "stdlib/result.l"

// Mutable globals (Rule 39: explicit type declaration + `!`).
i32 log_level! = 1
i32 log_sink! = 0
String log_file_path! = String("")
bool log_file_ready! = false

pub fun log_set_level(i32 level) -> void {
    if level < 0 {
        log_level! = 0
        return
    }
    if level > 4 {
        log_level! = 4
        return
    }
    log_level! = level
}

pub fun log_get_level() -> i32 {
    return log_level
}

// Send subsequent lines to `path`, appended. Console sink is disabled.
pub fun log_to_file(String path) -> Result<bool, String> {
    // Probe writability with an empty append before committing to the switch.
    w := append_all(path, String(""))
    if !w.is_ok() {
        return Result<bool, String>(false, false, w.unwrap_err())
    }
    log_file_path! = path
    log_file_ready! = true
    log_sink! = 1
    return Result<bool, String>(true, true, String(""))
}

pub fun log_to_console() -> void {
    log_sink! = 0
}

// Suppress output entirely without changing the level (useful in tests).
pub fun log_silence() -> void {
    log_sink! = 2
}

fun log_write(String line) -> void {
    if log_sink == 1 && log_file_ready {
        w := append_all(log_file_path, line + String("\n"))
        if w.is_ok() {
            return
        }
        // Fall back to console rather than losing the message.
    }
    if log_sink != 2 {
        print(line)
    }
}

fun log_emit(i32 level, String tag, String msg) -> void {
    if level < log_level {
        return
    }
    if log_sink == 2 {
        return
    }
    ts := format_time(time_now(), String("%Y-%m-%d %H:%M:%S"))
    log_write(tag + ts + String(" ") + msg)
}

// Structured field: `log_info_kv("cache", "hit", "user=42")` →
//   [INFO] ts cache hit user=42
// Kept as separate parameters so no Map allocation is needed per call.
pub fun log_kv(i32 level, String tag, String key, String value, String extra) -> void {
    if level < log_level {
        return
    }
    if log_sink == 2 {
        return
    }
    line := tag.concat(key).concat(String("=")).concat(value)
    if extra.length() > 0 {
        line = line.concat(String(" ")).concat(extra)
    }
    ts := format_time(time_now(), String("%Y-%m-%d %H:%M:%S"))
    log_write(ts.concat(String(" ")).concat(line))
}

pub fun log_debug(String msg) -> void {
    log_emit(0, String("[DEBUG] "), msg)
}

pub fun log_info(String msg) -> void {
    log_emit(1, String("[INFO] "), msg)
}

pub fun log_warn(String msg) -> void {
    log_emit(2, String("[WARN] "), msg)
}

pub fun log_error(String msg) -> void {
    log_emit(3, String("[ERROR] "), msg)
}

// Structured variants — same levels, key/value payload.
pub fun log_info_kv(String key, String value) -> void {
    log_kv(1, String("[INFO] "), key, value, String(""))
}

pub fun log_warn_kv(String key, String value) -> void {
    log_kv(2, String("[WARN] "), key, value, String(""))
}

pub fun log_error_kv(String key, String value) -> void {
    log_kv(3, String("[ERROR] "), key, value, String(""))
}
