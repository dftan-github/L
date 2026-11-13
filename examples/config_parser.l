// Simple #comment / key = value config parser (ENV / TOML-lite).
// Ignores blank lines and full-line comments; trims keys and values.

fun is_space(i32 c) -> bool {
    return c == 32 || c == 9 || c == 13
}

fun trim(String s) -> String {
    n := s.length()
    start! := 0
    while start < n && is_space(s.char_at(start)) {
        start! = start + 1
    }
    end! := n
    while end > start && is_space(s.char_at(end - 1)) {
        end! = end - 1
    }
    return s.slice(start, end - start)
}

fun index_of_char(String s, i32 ch) -> i32 {
    n := s.length()
    i! := 0
    while i < n {
        if s.char_at(i) == ch {
            return i
        }
        i! = i + 1
    }
    return 0 - 1
}

pub fun parse_config(String content) -> Map<String> {
    cfg := Map<String>()
    lines := content.split(10)
    for line in lines {
        t := trim(line)
        if t.length() == 0 {
            // blank — skip
        } else {
            if t.char_at(0) == 35 {
                // # comment — skip
            } else {
                eq := index_of_char(t, 61)
                if eq < 0 {
                    // no '=' — skip
                } else {
                    key := trim(t.slice(0, eq))
                    value := trim(t.slice(eq + 1, t.length() - eq - 1))
                    if key.length() > 0 {
                        cfg.put(key, value)
                    }
                }
            }
        }
    }
    return cfg
}
