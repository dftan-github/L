// Minimal String wrapper over a C string pointer.
// Requires stdlib/io.l (l_print_str / l_str_concat / l_read_line / l_str_*).
class String {
    ptr data

    fun init(ptr d) {
        data = d
    }

    fun printSelf() -> void {
        l_print_str(data)
    }

    // Expose the underlying C string for Map / interop (read-only pointer).
    pub fun c_str() -> ptr {
        return data
    }

    pub fun length() -> i32 {
        return l_str_len(data)
    }

    pub fun char_at(i32 i) -> i32 {
        return l_str_char_at(data, i)
    }

    pub fun slice(i32 start, i32 len) -> String {
        ptr p = l_str_substr(data, start, len)
        return String(p)
    }

    // True when this string begins with `prefix` (byte-wise).
    pub fun starts_with(String prefix) -> bool {
        n := prefix.length()
        if n > length() {
            return false
        }
        i! := 0
        while i < n {
            if char_at(i) != prefix.char_at(i) {
                return false
            }
            i! = i + 1
        }
        return true
    }

    pub fun ends_with(String suffix) -> bool {
        n := suffix.length()
        m := length()
        if n > m {
            return false
        }
        base := m - n
        i! := 0
        while i < n {
            if char_at(base + i) != suffix.char_at(i) {
                return false
            }
            i! = i + 1
        }
        return true
    }

    // Index of first occurrence of `needle`, or -1.
    pub fun index_of(String needle) -> i32 {
        nlen := needle.length()
        m := length()
        if nlen == 0 {
            return 0
        }
        if nlen > m {
            return 0 - 1
        }
        i! := 0
        while i <= m - nlen {
            same! := true
            j! := 0
            while j < nlen {
                if char_at(i + j) != needle.char_at(j) {
                    same! = false
                    j! = nlen
                } else {
                    j! = j + 1
                }
            }
            if same {
                return i
            }
            i! = i + 1
        }
        return 0 - 1
    }

    pub fun contains(String needle) -> bool {
        idx := index_of(needle)
        return idx >= 0
    }

    // Strip leading/trailing ASCII space, tab, CR, LF.
    pub fun trim() -> String {
        m := length()
        s! := 0
        e! := m
        cont! := true
        while cont {
            if s >= e {
                cont! = false
            } else {
                c := char_at(s)
                if c == 32 || c == 9 || c == 10 || c == 13 {
                    s! = s + 1
                } else {
                    cont! = false
                }
            }
        }
        cont! = true
        while cont {
            if s >= e {
                cont! = false
            } else {
                c := char_at(e - 1)
                if c == 32 || c == 9 || c == 10 || c == 13 {
                    e! = e - 1
                } else {
                    cont! = false
                }
            }
        }
        ptr p = l_str_substr(data, s, e - s)
        return String(p)
    }

    // Replace first occurrence of `from` with `to`.
    pub fun replace(String from, String to) -> String {
        idx := index_of(from)
        if idx < 0 {
            ptr p0 = l_str_substr(data, 0, length())
            return String(p0)
        }
        ptr left_p = l_str_substr(data, 0, idx)
        left := String(left_p)
        mid := left.concat(to)
        right_start := idx + from.length()
        ptr right_p = l_str_substr(data, right_start, length() - right_start)
        right := String(right_p)
        return mid.concat(right)
    }

    // Replace all non-overlapping occurrences of `from` with `to`.
    pub fun replace_all(String from, String to) -> String {
        // Phase 1: iterative first-replace via replace() (adequate for small strings).
        ptr p0 = l_str_substr(data, 0, length())
        cur! := String(p0)
        guard! := 0
        cont! := true
        while cont {
            if guard >= 64 {
                cont! = false
            } else {
                if cur.index_of(from) < 0 {
                    cont! = false
                } else {
                    nxt := cur.replace(from, to)
                    cur! = nxt
                    guard! = guard + 1
                }
            }
        }
        return cur
    }

    // Split on a single ASCII separator code point (e.g. 32 = space, 61 = '=').
    pub fun split(i32 sep) -> Array<String> {
        arr := Array<String>()
        n := length()
        start! := 0
        i! := 0
        while i < n {
            c := char_at(i)
            if c == sep {
                if i > start {
                    ptr p = l_str_substr(data, start, i - start)
                    arr.push(String(p))
                }
                start! = i + 1
            }
            i! = i + 1
        }
        if start < n {
            ptr p2 = l_str_substr(data, start, n - start)
            arr.push(String(p2))
        }
        return arr
    }

    pub fun to_i32() -> i32 {
        return l_str_to_i32(data)
    }

    pub fun concat(String other) -> String {
        ptr new_data = l_str_concat(data, other.data)
        return String(new_data)
    }
}

// ASCII helpers (i32 code points; L has no char type yet).
pub fun is_digit(i32 c) -> bool {
    return c >= 48 && c <= 57
}

pub fun is_alpha(i32 c) -> bool {
    return (c >= 65 && c <= 90) || (c >= 97 && c <= 122)
}

pub fun from(i32 n) -> String {
    ptr data = l_int_to_str(n)
    return String(data)
}

pub fun from_i64(i64 n) -> String {
    ptr data = l_i64_to_str(n)
    return String(data)
}

pub fun str_from_f64(f64 n) -> String {
    ptr data = l_f64_to_str(n)
    return String(data)
}

pub fun str_from_bool(bool b) -> String {
    v! := 0
    if b {
        v! = 1
    }
    ptr data = l_bool_to_str(v)
    return String(data)
}

pub fun read_line() -> String {
    ptr data = l_read_line()
    return String(data)
}
