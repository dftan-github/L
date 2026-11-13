// Phase-1 regex helpers (runtime/regex.c portable mini-engine).
extern module c_regex {
    fun l_regex_match(ptr pattern, ptr text) -> i32
}

pub fun contains_pattern(String text, String pattern) -> bool {
    if l_regex_match(pattern.c_str(), text.c_str()) != 0 {
        return true
    }
    return false
}
