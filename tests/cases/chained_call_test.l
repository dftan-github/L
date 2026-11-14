// Minimal, focused assertions for chained dispatch on interface values.
// Every call below goes straight from `arr.get(i)` to the method — no
// intermediate variable — covering void, i32 and String returns.

interface IShape {
    fun area() -> i32
    fun kind() -> String
    fun touch()
}

class Square : IShape {
    i32 side

    fun init(i32 s) {
        side = s
    }

    fun area() -> i32 {
        return side * side
    }

    fun kind() -> String {
        return String("square")
    }

    fun touch() {
        print(String("touch square"))
    }
}

class Rect : IShape {
    i32 w
    i32 h

    fun init(i32 a, i32 b) {
        w = a
        h = b
    }

    fun area() -> i32 {
        return w * h
    }

    fun kind() -> String {
        return String("rect")
    }

    fun touch() {
        print(String("touch rect"))
    }
}

fun main() -> i32 {
    shapes := Array<IShape>()
    shapes.push(Square(3))
    shapes.push(Rect(2, 5))

    // i32-returning chained call
    print(shapes.get(0).area())
    print(shapes.get(1).area())

    // String-returning chained call
    print(shapes.get(0).kind())
    print(shapes.get(1).kind())

    // void-returning chained call
    shapes.get(0).touch()
    shapes.get(1).touch()

    // Chained call on a chained call: element method -> returned value
    print(shapes.get(0).kind().length())

    return 0
}
