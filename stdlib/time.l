// Wall-clock helpers (C: l_time_now_* / l_format_time in runtime/sys.c; sleep → l_sleep_ms).

pub fun time_now() -> i64 {
    return l_time_now_sec()
}

pub fun time_now_ms() -> i64 {
    return l_time_now_ms()
}

pub fun sleep(i32 ms) -> void {
    l_sleep_ms(ms)
}

pub fun format_time(i64 timestamp, String format) -> String {
    ptr p = l_format_time(timestamp, format.c_str())
    return String(p)
}

// Non-blocking delayed callback: spawn a worker that sleeps then runs `callback`.
// Call sites with an inline trailing block / lambda are desugared by Codegen into
// `spawn { sleep(ms); body }` so captures stay in the caller's frame.
pub fun delay(i32 ms, fun() callback) -> void {
    spawn {
        sleep(ms)
        callback()
    }
}
