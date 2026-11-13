fun safe_div(i32 a, i32 b) -> Result<i32, String> {
    if b == 0 {
        return Result<i32, String>(false, 0, "Div by zero")
    }
    return Result<i32, String>(true, a / b, "")
}

// Using ? requires returning Result — errors propagate upward.
fun calc(i32 x) -> Result<i32, String> {
    val := safe_div(10, x)?
    return Result<i32, String>(true, val + 5, "")
}

fun main() -> i32 {
    r1 := calc(2)
    print(r1.unwrap())

    r2 := calc(0)
    print(r2.unwrap_err())
    return 0
}
