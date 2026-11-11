interface IReader {
    fun read_value() -> i32;
}

class Point : IReader {
    i32 x;
    fun read_value() -> i32 {
        return x;
    }
}

fun print_val<T: IReader>(T item) {
    i32 v = item.read_value();
    print(v);
}

fun main() -> i32 {
    Point p = Point();
    p.x = 88;
    print_val(p);
    return 0;
}
