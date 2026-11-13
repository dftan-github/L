fun do_work(i32 times) {
    // 循环创建堆对象并自动回收
    for i in 0..times {
        // 使用 heap 和 String 拼接
        s := heap String("Test")
        t := s + " String"
        // 注意：t 也是 heap 分配的，如果没有释放，这里会泄漏
    }
}

fun main() -> i32 {
    before := l_get_alloc_count()

    do_work(1000)

    after := l_get_alloc_count()

    if before == after {
        print("No memory leaks!")
    } else {
        print("Memory leak detected!")
    }
    return 0
}
