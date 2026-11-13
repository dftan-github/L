class Point {
    i32 x
    i32 y
    fun init(i32 a, i32 b) {
        x = a
        y = b
    }
}

fun main() -> i32 {
    p := Point(10, 20)
    print(p.to_string())
    return 0
}
