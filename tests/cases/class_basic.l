class Point {
    i32 x;
    i32 y;
}

fun main() -> i32 {
    Point p = Point();
    p.x = 10;
    p.y = 20;
    i32 sum = p.x + p.y;
    print(sum);
    return 0;
}
