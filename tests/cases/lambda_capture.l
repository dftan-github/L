fun main() -> i32 {
    i32 base = 100

    // 捕获了外部的 base 变量
    add_base := |x| x + base

    res := add_base(5)
    print(res)
    return 0
}
