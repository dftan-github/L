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

fun main() -> i32 {
    p := Point(42)
    r := p as IReader

    // Downcast success → Optional<Point>
    p_opt := r as Point
    if p_opt.is_some() {
        print(p_opt.unwrap().x)
    }

    // Wrong class → empty Optional
    u_opt := r as User
    if !u_opt.is_some() {
        print("safe")
    }
    return 0
}
