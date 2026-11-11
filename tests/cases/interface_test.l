interface IReader {
    fun read_value() -> i32;
}

class Point : IReader {
    i32 x;
    fun read_value() -> i32 {
        return x;
    }
}

fun main() -> i32 {
    Point p = Point();
    p.x = 42;

    IReader iface = p;
    i32 result = iface.read_value();
    print(result);
    return 0;
}
