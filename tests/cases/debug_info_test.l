// Debug-info probe: `-g` must attach DWARF line tables to this function body.
fun add(i32 a, i32 b) -> i32 {
    c := a + b
    return c
}

fun main() -> i32 {
    v := add(20, 22)
    print(v)
    return 0
}
