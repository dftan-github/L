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

fun do_heavy_work() {
    for i in 0..1000 {
        // Scene 1: string concat intermediate objects
        s1 := String("Hello")
        s2 := String(" World")
        s3 := s1 + s2

        // Scene 2: interface boxing into Array
        arr := Array<IReader>()
        arr.push(Point(i))

        // Scene 3: capturing closure call
        closure := |x| x + i
        closure(10)
    }
}

fun main() -> i32 {
    before := l_get_alloc_count()

    do_heavy_work()

    after := l_get_alloc_count()

    print("Before: " + before)
    print("After: " + after)

    if before == after {
        print("No memory leaks!")
    } else {
        print("Memory leak detected!")
    }
    return 0
}
