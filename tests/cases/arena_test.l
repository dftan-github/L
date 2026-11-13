fun build_string(i32 n) -> String {
    // 申请一个大内存池
    a := arena(1024)

    // 在该内存池上疯狂分配，不需要逐次 free
    s1 := a.new_string("Hello")
    s2 := a.new_string(" World")

    res := s1 + s2 // 结果走普通堆，可安全逃离 arena 作用域
    return res
    // 函数结束时，a 内存池会自动 l_arena_free，一次性清空 bump 分配
}

fun main() -> i32 {
    res := build_string(1)
    print(res)
    return 0
}
