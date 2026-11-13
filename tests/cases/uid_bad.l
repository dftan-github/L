fun main() -> i32 {
    // Runtime call must be rejected (compile-time only).
    x := uid_counter()
    print(x)
    return 0
}
