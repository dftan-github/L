// Structured logging (timestamp + level + message) with a global level filter.
//
// Levels: 0 = DEBUG, 1 = INFO, 2 = WARN, 3 = ERROR, 4 = OFF.
// Default is INFO. `log_set_level` filters at emit time, so `log_debug` in a hot
// path costs one comparison plus one format call when enabled.

// Mutable global (Rule 39: explicit type declaration + `!` for mutability).
i32 log_level! = 1

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

fun log_emit(i32 level, String tag, String msg) -> void {
    if level < log_level {
        return
    }
    ts := format_time(time_now(), String("%Y-%m-%d %H:%M:%S"))
    print(tag + ts + String(" ") + msg)
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
