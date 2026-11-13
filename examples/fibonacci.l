fun fibonacci(i32 n) -> i32 {
    if n <= 1 {
        return n
    }
    i32 a! = 0
    i32 b! = 1
    i32 temp! = 0
    i32 i! = 2
    
    while i <= n {
        temp! = a + b
        a! = b
        b! = temp
        i! = i + 1
    }
    return b
}

fun main() -> i32 {
    print("Fibonacci Sequence:")
    i32 i! = 0
    while i < 10 {
        res := fibonacci(i)
        print(res)
        i! = i + 1
    }
    return 0
}