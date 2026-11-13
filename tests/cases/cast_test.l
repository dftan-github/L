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
    // Upcast (auto-box to fat pointer)
    IReader r = Point(42)

    // Downcast (success)
    p_opt := r as Point
    if p_opt.is_some() {
        print(p_opt.unwrap().x)
    }

    // Downcast (fail: r is Point, not User)
    u_opt := r as User
    if !u_opt.is_some() {
        print("Not a User")
    }
    return 0
}
