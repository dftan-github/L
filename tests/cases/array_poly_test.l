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
    arr := Array<IReader>()

    arr.push(Point(42))
    arr.push(User(99))

    i32 sum! = 0
    for item in arr {
        sum! += item.read_value()
    }

    print(sum)
    return 0
}
