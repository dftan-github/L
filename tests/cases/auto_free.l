class Box {
    i32 val
    fun init(i32 v) {
        val = v
    }
}

fun create_and_leak() {
    b := heap Box(42)
    // Intentionally no defer l_free(b) — compiler must auto-inject.
    print(b.val)
}

fun main() -> i32 {
    create_and_leak()
    print("Done")
    return 0
}
