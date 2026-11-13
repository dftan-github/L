class Box {
    i32 val
    fun init(i32 v) {
        val = v
    }
}

fun try_modify(Box b) {
    print(b.val)
}

fun real_modify(Box b!) {
    b.val = 99
}

fun main() -> i32 {
    b := Box(10)
    try_modify(b)
    real_modify(b)
    print(b.val)
    return 0
}
