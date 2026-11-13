// Minimal space-splitting lexer in pure L.
// Classifies each part as number (Variant i32) or identifier/symbol (Variant String).

fun is_number_token(String s) -> bool {
    n := s.length()
    if n == 0 {
        return false
    }
    i! := 0
    while i < n {
        if is_digit(s.char_at(i)) == false {
            return false
        }
        i! = i + 1
    }
    return true
}

fun is_ident_token(String s) -> bool {
    n := s.length()
    if n == 0 {
        return false
    }
    if is_alpha(s.char_at(0)) == false {
        return false
    }
    i! := 1
    while i < n {
        c := s.char_at(i)
        if is_alpha(c) == false {
            if is_digit(c) == false {
                return false
            }
        }
        i! = i + 1
    }
    return true
}

pub fun tokenize(String src) -> Array<Variant> {
    tokens := Array<Variant>()
    parts := src.split(32)
    i! := 0
    while i < parts.len() {
        part := parts.get(i)
        if is_number_token(part) {
            tokens.push(Variant.from_i32(part.to_i32()))
        } else {
            tokens.push(Variant.from_string(part))
        }
        i! = i + 1
    }
    return tokens
}
