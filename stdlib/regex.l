// Phase-1 regex helpers (runtime/regex.c portable mini-engine).
//
// Return codes from the C engine:
//   1  → match
//   0  → no match
//  -1  → engine error: a `*`/`+` repetition exceeded the bounded backtrack
//        stack. Reporting a plain "no match" there would be a lie, so it is
//        surfaced as a hard runtime error instead of a silent misresult.
extern module c_regex {
    fun l_regex_match(ptr pattern, ptr text) -> i32
    fun l_panic(ptr message) -> void
}

pub fun contains_pattern(String text, String pattern) -> bool {
    code := l_regex_match(pattern.c_str(), text.c_str())
    if code < 0 {
        msg := String("regex: repetition too long for the backtrack stack")
        l_panic(msg.c_str())
    }
    if code != 0 {
        return true
    }
    return false
}
