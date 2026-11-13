fun main() -> i32 {
    // 1. Array literal
    arr := [10, 20, 30]

    // 2. Trailing-block each
    arr.each { |x|
        print(x * 2)
    }
    return 0
}
