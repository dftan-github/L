// `break` outside any loop must be rejected at parse time.
fun main() -> i32 {
    print(1)
    break
    return 0
}
