fun main() -> i32 {
    arr := [1, 2, 3]
    i32 sum! = 0
    arr.each { |x|
        sum! = sum + x
    }
    print(sum)
    return 0
}
