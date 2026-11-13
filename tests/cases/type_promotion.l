fun main() -> i32 {
    a32 := 10
    i64 b64 = 20
    c_float := 3.14

    // i32 + i64 -> 自动提升为 i64
    res1 := a32 + b64
    print(res1)

    // i32 * f64 -> 自动提升为 f64
    res2 := a32 * c_float
    print(res2)

    return 0
}
