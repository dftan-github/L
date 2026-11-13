class Pair {
    i32 first
    i32 second
    fun init(i32 a, i32 b) {
        first = a
        second = b
    }
}

fun main() -> i32 {
    p := Pair(10, 20)
    // Destructure class fields in declaration order
    (x, y) := p

    print(x)
    print(y)
    return 0
}
