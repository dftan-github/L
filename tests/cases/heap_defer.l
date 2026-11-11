class Point {
    i32 x;
}

fun main() -> i32 {
    Point p = heap Point();
    defer l_free(p);

    p.x = 77;
    print(p.x);
    return 0;
}
