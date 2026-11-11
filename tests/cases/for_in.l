fun main() -> i32 {
    arr := Array<i32>()
    arr.push(5)
    arr.push(10)
    arr.push(15)

    sum := 0
    for val in arr {
        sum = sum + val
    }
    print(sum)
    return 0
}
