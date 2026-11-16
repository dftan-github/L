// [A005] Negative case: a bare string literal with `:=` needs stdlib/string.l.
// Compiled WITHOUT string.l on purpose — must fail with a diagnostic that names
// stdlib/string.l instead of degrading to ptr and failing later in print().

fun main() -> i32 {
    a := "hello"
    print(a)
    return 0
}
