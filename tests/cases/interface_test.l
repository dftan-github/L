interface IReader {
    fun read_value() -> i32
}

class Point : IReader {
    i32 x
    fun read_value() -> i32 {
        return x
    }
}

fun main() -> i32 {
    p := Point()
    p.x = 42

    // Interface binding needs an explicit type (:= would infer Point).
    IReader iface = p
    result := iface.read_value()
    print(result)
    return 0
}
