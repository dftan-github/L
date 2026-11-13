class Point {
    i32 x
    i32 y
    fun init(i32 a, i32 b) {
        x = a
        y = b
    }
}

fun main() -> i32 {
    a := Point(10, 20)
    // 使用编译器自动生成的 clone
    b := a.clone()

    b.x = 99

    print(a.x)
    print(b.x)
    return 0
}
