class Box {
    i32 val
    fun init(i32 v) {
        val = v
    }
}

fun try_modify(Box b) {
    b.val = 99
    print(b.val)
}

fun main() -> i32 {
    b := Box(10)
    try_modify(b)
    return 0
}
