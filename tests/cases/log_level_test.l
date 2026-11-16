// P0-3: log level filtering. Default is INFO; setting a higher level must
// suppress lower-severity lines, and OFF must suppress everything.
fun main() -> i32 {
    // Default level is INFO → DEBUG suppressed, INFO/WARN/ERROR emitted.
    if log_get_level() != 1 {
        print(String("fail: default level"))
        return 1
    }
    print(String("level_default_ok"))

    log_set_level(2)
    if log_get_level() != 2 {
        print(String("fail: set level"))
        return 1
    }
    log_debug(String("debug_hidden"))
    log_info(String("info_hidden"))
    log_warn(String("warn_visible"))
    log_error(String("error_visible"))

    // OFF must silence everything, including errors.
    log_set_level(4)
    log_error(String("error_hidden"))

    // Out-of-range values clamp instead of corrupting state.
    log_set_level(99)
    if log_get_level() != 4 {
        print(String("fail: clamp high"))
        return 1
    }
    log_set_level(0 - 5)
    if log_get_level() != 0 {
        print(String("fail: clamp low"))
        return 1
    }
    log_debug(String("debug_visible"))

    print(String("log_level_ok"))
    return 0
}
