fun check(i32 x) {
    match x {
        1 => { print("One") }
        2 => { print("Two") }
        _ => { print("Other") }
    }
}

fun main() -> i32 {
    check(1)
    check(2)
    check(99)
    return 0
}
