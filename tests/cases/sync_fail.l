fun main() -> i32 {
    mut_count! := 0
    spawn {
        mut_count! = 1
    }
    return 0
}
