// Optional<T> chained unwrap: `opt.unwrap().method()` must dispatch straight
// off the unwrapped receiver — for plain classes and for interface fat pointers.

interface IShape {
    fun kind() -> String
    fun sides() -> i32
}

class Triangle : IShape {
    pub i32 n
    fun init(i32 count) {
        n = count
    }

    fun kind() -> String {
        return String("triangle")
    }

    fun sides() -> i32 {
        return n
    }
}

class Circle : IShape {
    pub i32 n
    fun init(i32 count) {
        n = count
    }

    fun kind() -> String {
        return String("circle")
    }

    fun sides() -> i32 {
        return n
    }
}

fun some_shape() -> Optional<IShape> {
    return Optional<IShape>(true, Triangle(3))
}

fun no_shape() -> Optional<IShape> {
    return Optional<IShape>(false, Circle(0))
}

fun main() -> i32 {
    s := some_shape()
    // Interface payload: unwrap yields the fat pointer, then vtable dispatch.
    print(s.unwrap().kind())
    print(s.unwrap().sides())

    // Chained directly off the call — no intermediate local.
    print(some_shape().unwrap().kind())

    // unwrap_or fallback keeps the same dispatch path.
    print(no_shape().unwrap_or(Circle(1)).sides())

    // Plain class payload: unwrap yields Class*, member call works.
    o := Optional<Circle>(true, Circle(7))
    print(o.unwrap().sides())
    return 0
}
