fun main() -> i32 {
    i32 a! = 10

    // 复合赋值
    a! += 5
    a! -= 3

    // 自增/自减（作为语句）
    a!++
    ++a!
    a!--

    print(a)

    // 作为表达式
    i32 b! = 5
    i32 c = b!++
    i32 d = ++b!

    print(c)
    print(b)
    print(d)

    return 0
}
