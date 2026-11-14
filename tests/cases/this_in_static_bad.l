// `this` inside a static method must be a compile error.
class Counter {
    i32 n

    static fun make() -> i32 {
        return this.n
    }
}

fun main() -> i32 {
    return Counter.make()
}
