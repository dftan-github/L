class MathUtil {
    static pub fun add(i32 a, i32 b) -> i32 {
        return a + b
    }
}

fun main() -> i32 {
    // Call via class name — no instance required.
    res := MathUtil.add(10, 20)
    print(res)
    return 0
}
