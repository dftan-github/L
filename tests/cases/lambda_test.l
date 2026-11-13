// fun(i32) alone means void callback (e.g. Array.each); returning a value needs ->i32.
fun apply(fun(i32)->i32 f, i32 val) -> i32 {
    return f(val)
}

fun main() -> i32 {
    // 1. 直接作为参数传入
    res1 := apply(|x| x * 2, 10)
    print(res1)

    // 2. 赋值给变量
    double := |x| x * 2
    res2 := double(5)
    print(res2)
    return 0
}
