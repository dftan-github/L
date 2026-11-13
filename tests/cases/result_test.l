fun safe_div(i32 a, i32 b) -> Result<i32, String> {
    if b == 0 {
        return Result<i32, String>(false, 0, String("Division by zero"))
    }
    return Result<i32, String>(true, a / b, String(""))
}

fun main() -> i32 {
    res1 := safe_div(10, 2)
    print(res1.unwrap())

    res2 := safe_div(10, 0)
    print(res2.unwrap_err())

    return 0
}
