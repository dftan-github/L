// Structured logging (timestamp + level + message).

pub fun log_info(String msg) -> void {
    ts := format_time(time_now(), String("%Y-%m-%d %H:%M:%S"))
    print(String("[INFO] ") + ts + String(" ") + msg)
}

pub fun log_warn(String msg) -> void {
    ts := format_time(time_now(), String("%Y-%m-%d %H:%M:%S"))
    print(String("[WARN] ") + ts + String(" ") + msg)
}

pub fun log_error(String msg) -> void {
    ts := format_time(time_now(), String("%Y-%m-%d %H:%M:%S"))
    print(String("[ERROR] ") + ts + String(" ") + msg)
}
