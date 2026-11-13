extern module c_lib {
    fun strlen(ptr s) -> i64
}

fun main() -> i32 {
    len := strlen("Hello")
    print(len)
    return 0
}
