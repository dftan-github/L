// Phase-1 JSON: flat objects → Map<String>. Nested objects/arrays rejected.
// Values stored as decoded text (strings unquoted; true/false/null/numbers as text).

fun json_lbrace() -> String {
    return String(l_str_from_byte(123))
}

fun json_rbrace() -> String {
    return String(l_str_from_byte(125))
}

fun json_quote() -> String {
    return String(l_str_from_byte(34))
}

fun json_skip_ws(String s, i32 i) -> i32 {
    n := s.length()
    j! := i
    while j < n {
        c := s.char_at(j)
        if c == 32 || c == 9 || c == 10 || c == 13 {
            j! = j + 1
        } else {
            return j
        }
    }
    return j
}

fun json_string_end(String s, i32 i) -> i32 {
    n := s.length()
    if i >= n || s.char_at(i) != 34 {
        return 0 - 1
    }
    j! := i + 1
    while j < n {
        c := s.char_at(j)
        if c == 34 {
            return j + 1
        }
        if c == 92 {
            j! = j + 1
        }
        j! = j + 1
    }
    return 0 - 1
}

fun json_decode_string(String s, i32 start, i32 end_excl) -> String {
    inner := s.slice(start + 1, end_excl - start - 2)
    a := inner.replace_all(String("\\n"), String("\n"))
    b := a.replace_all(String("\\t"), String("\t"))
    q := json_quote()
    bs := String("\\")
    esc_q := bs.concat(q)
    c := b.replace_all(esc_q, q)
    esc_bs := bs.concat(bs)
    return c.replace_all(esc_bs, bs)
}

fun json_parse_value_text(String s, i32 i) -> Optional<String> {
    n := s.length()
    j := json_skip_ws(s, i)
    if j >= n {
        return Optional<String>()
    }
    c := s.char_at(j)
    if c == 34 {
        e := json_string_end(s, j)
        if e < 0 {
            return Optional<String>()
        }
        return Optional<String>(true, json_decode_string(s, j, e))
    }
    if c == 116 {
        if j + 4 <= n {
            tok := s.slice(j, 4)
            if tok.starts_with(String("true")) {
                return Optional<String>(true, String("true"))
            }
        }
        return Optional<String>()
    }
    if c == 102 {
        if j + 5 <= n {
            tok := s.slice(j, 5)
            if tok.starts_with(String("false")) {
                return Optional<String>(true, String("false"))
            }
        }
        return Optional<String>()
    }
    if c == 110 {
        if j + 4 <= n {
            tok := s.slice(j, 4)
            if tok.starts_with(String("null")) {
                return Optional<String>(true, String("null"))
            }
        }
        return Optional<String>()
    }
    k! := j
    cont! := true
    while cont {
        if k >= n {
            cont! = false
        } else {
            ch := s.char_at(k)
            if is_digit(ch) || ch == 45 || ch == 43 || ch == 46 || ch == 101 || ch == 69 {
                k! = k + 1
            } else {
                cont! = false
            }
        }
    }
    if k == j {
        return Optional<String>()
    }
    return Optional<String>(true, s.slice(j, k - j))
}

fun json_value_end(String s, i32 i) -> i32 {
    n := s.length()
    j := json_skip_ws(s, i)
    if j >= n {
        return 0 - 1
    }
    c := s.char_at(j)
    if c == 34 {
        return json_string_end(s, j)
    }
    if c == 116 {
        return j + 4
    }
    if c == 102 {
        return j + 5
    }
    if c == 110 {
        return j + 4
    }
    k! := j
    cont! := true
    while cont {
        if k >= n {
            cont! = false
        } else {
            ch := s.char_at(k)
            if is_digit(ch) || ch == 45 || ch == 43 || ch == 46 || ch == 101 || ch == 69 {
                k! = k + 1
            } else {
                cont! = false
            }
        }
    }
    if k == j {
        return 0 - 1
    }
    return k
}

pub fun json_parse_object(String text) -> Result<Map<String>, String> {
    s := text.trim()
    m := Map<String>()
    if s.length() < 2 {
        return Result<Map<String>, String>(false, m, String("json: empty"))
    }
    if s.char_at(0) != 123 {
        return Result<Map<String>, String>(false, m, String("json: expected object"))
    }
    i! := json_skip_ws(s, 1)
    n := s.length()
    if i < n {
        if s.char_at(i) == 125 {
            return Result<Map<String>, String>(true, m, String(""))
        }
    }
    cont! := true
    while cont {
        i! = json_skip_ws(s, i)
        if i >= n || s.char_at(i) != 34 {
            return Result<Map<String>, String>(false, m, String("json: expected key"))
        }
        ke := json_string_end(s, i)
        if ke < 0 {
            return Result<Map<String>, String>(false, m, String("json: bad key"))
        }
        key := json_decode_string(s, i, ke)
        i! = json_skip_ws(s, ke)
        if i >= n || s.char_at(i) != 58 {
            return Result<Map<String>, String>(false, m, String("json: expected colon"))
        }
        i! = i + 1
        val_opt := json_parse_value_text(s, i)
        if val_opt.is_some() == false {
            return Result<Map<String>, String>(false, m, String("json: bad value"))
        }
        m.put(key, val_opt.unwrap())
        i! = json_value_end(s, i)
        if i < 0 {
            return Result<Map<String>, String>(false, m, String("json: bad value end"))
        }
        i! = json_skip_ws(s, i)
        if i >= n {
            return Result<Map<String>, String>(false, m, String("json: unexpected end"))
        }
        if s.char_at(i) == 125 {
            return Result<Map<String>, String>(true, m, String(""))
        }
        if s.char_at(i) != 44 {
            return Result<Map<String>, String>(false, m, String("json: expected comma or end"))
        }
        i! = i + 1
    }
    return Result<Map<String>, String>(false, m, String("json: internal"))
}

fun json_escape(String s) -> String {
    bs := String("\\")
    q := json_quote()
    a := s.replace_all(bs, bs.concat(bs))
    b := a.replace_all(q, bs.concat(q))
    c := b.replace_all(String("\n"), String("\\n"))
    return c.replace_all(String("\t"), String("\\t"))
}

// Phase-1 stringify: only emits a non-empty object marker (full round-trip later).
pub fun json_stringify_object(Map<String> obj) -> String {
    n := obj.len()
    if n == 0 {
        String a = json_lbrace()
        String b = json_rbrace()
        return a.concat(b)
    }
    String a = json_lbrace()
    String mid = String("...")
    String b = json_rbrace()
    String t = a.concat(mid)
    return t.concat(b)
}
