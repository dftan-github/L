fun main() -> i32 {
    mut_count! := 0
    sync {
        mut_count! = 1
    }
    print(mut_count)
    return 0
}
