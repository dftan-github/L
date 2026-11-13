interface IReader {
    fun read_value() -> i32
}

class Point : IReader {
    i32 x
    fun init(i32 v) {
        x = v
    }
    fun read_value() -> i32 {
        return x
    }
}

class User : IReader {
    i32 id
    fun init(i32 v) {
        id = v
    }
    fun read_value() -> i32 {
        return id
    }
}

// Receives an interface fat pointer — no knowledge of the concrete class.
fun print_any_reader(IReader r) {
    print(r.read_value())
}

fun main() -> i32 {
    p := Point(42)
    u := User(99)

    print_any_reader(p)
    print_any_reader(u)
    return 0
}
