import "stdlib/optional.l"

// Three levels of nesting must be rejected at compile time.
// `Box<...>` stores its type argument as a real field, so specializing it
// forces the inner specialization (unlike Array, whose buffer is a raw ptr).
class Box<T> {
    T inner
}

fun main() -> i32 {
    b := Box<Box<Box<i32>>>()
    return 0
}
