comptime {
    compile_error("Build forbidden: This is a forced compile error.")
}

fun main() -> i32 {
    return 0
}
