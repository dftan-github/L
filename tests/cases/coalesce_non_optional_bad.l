fun main() -> i32 {
    // `??` on a plain i32 must be rejected: the left side is not Optional<T>.
    x := 5
    y := x ?? 7
    print(y)
    return 0
}
