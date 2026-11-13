extern module c_runtime {
    fun l_register_callback(fun(i32) cb)
    fun l_trigger_callback(i32 val)
}

fun my_callback(i32 v) {
    print("Callback triggered with: ")
    print(v)
}

fun main() -> i32 {
    l_register_callback(my_callback)
    l_trigger_callback(42)
    return 0
}
