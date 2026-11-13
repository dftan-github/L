// Performance smoke: arithmetic loop + arena bump allocations.

class Cell {
    i32 v
    fun init(i32 x) {
        v = x
    }
}

fun bench_arith() -> i64 {
    // 100_000_000 iterations; i64 avoids overflow on the triangular sum.
    i64 sum! = 0
    i64 i! = 0
    while i < 100000000 {
        sum! += i
        i! += 1
    }
    return sum
}

fun bench_arena() -> i32 {
    // 1_000_000 bump allocations inside one arena (no per-object free).
    a := arena(16777216)
    i! := 0
    while i < 1000000 {
        c := a.new(Cell(i))
        i! += 1
    }
    return 0
}

fun main() -> i32 {
    print("bench_arith begin")
    s := bench_arith()
    print(s)
    print("bench_arith end")

    print("bench_arena begin")
    n := bench_arena()
    print(n)
    print("bench_arena end")
    return 0
}
