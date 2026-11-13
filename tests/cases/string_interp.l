fun main() -> i32 {
    name := "L Language"
    version := 1

    // 测试模板字符串（包含变量和表达式）
    greeting := "Welcome to {name}, version {version}!"
    print(greeting)

    a := 10
    b := 20
    calc := "The sum is {a + b}."
    print(calc)
    return 0
}
