// Layer 2 stdin helpers. Depends on String (compile with stdlib/string.l).

pub fun read_line() -> String {
    ptr data = l_read_line()
    return String(data)
}
