fun main() -> i32 {
    opt1 := Optional<i32>(true, 42)
    print(opt1 ?? 0)

    opt2 := Optional<i32>(false, 0)
    print(opt2 ?? 99)
    return 0
}
