fun get_closure() -> fun() {
    a := 10
    return || a * 2
}

fun main() -> i32 {
    cb := get_closure()
    res := cb()
    print(res)
    return 0
}
