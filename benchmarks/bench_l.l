// L vs C++ benchmark suite (native .exe, -O3).
// Three aligned workloads; each prints its own elapsed milliseconds.
//
//   A dense arithmetic loop      1e8 float accumulations (non-foldable shape)
//   B heap object creation       1e7 Point{i32,i32} objects over 10 arena batches
//   C string-keyed map           1e6 puts over a fixed 1e4-key pool
//
// Build (AOT, never `l eval` / the comptime interpreter):
//   l -O3 -o bench_l.exe stdlib/*.l benchmarks/bench_l.l

import "../stdlib/string.l"
import "../stdlib/optional.l"
import "../stdlib/array.l"
import "../stdlib/map.l"
import "../stdlib/time.l"

class Point {
    i32 x
    i32 y
    fun init(i32 a, i32 b) {
        x = a
        y = b
    }
}

// A. Dense arithmetic: 1e8 iterations of a float accumulation. Float addition is
// not reassociable, so LLVM cannot derive a closed form and the loop genuinely
// runs (an integer `sum += i*2+1` would be folded into one polynomial at -O3 on
// both compilers — verified). The C++ twin uses the exact same expression.
fun bench_arith() -> f64 {
    f64 sum = 0.0
    i32 i = 0
    while i < 100000000 {
        sum = sum + i * 0.5
        i = i + 1
    }
    return sum
}

// B. Heap object creation: 10 batches of 1e6 objects, each batch a two-i32 Point
// built from one arena block (one bump allocation serves the whole batch, then
// the block is released). 1e7 objects total so the run lands in the hundreds of
// milliseconds and the wall-clock timer resolves it reliably.
fun bench_objects() -> i64 {
    i64 acc! = 0
    i32 batch! = 0
    while batch < 10 {
        a := arena(16777216)
        i32 i! = 0
        while i < 1000000 {
            p := a.new(Point(i, i + 1))
            acc! = acc + p.x + p.y
            i! = i + 1
        }
        batch! = batch + 1
    }
    return acc
}

// C. String hashing: keyed map inserts over a fixed 1e4-key pool. Keys are built
// once (excluded from timing), then 1e6 puts (100 rounds x 1e4) measure the map's
// hash + probe path — not string construction. Same shape as the C++ twin.
fun bench_strings() -> i32 {
    keys := Array<String>()
    i32 i! = 0
    while i < 10000 {
        keys.push("key_" + i)
        i! = i + 1
    }
    m := Map<i32>()
    i32 round! = 0
    while round < 100 {
        i32 j! = 0
        while j < 10000 {
            m.put(keys.get(j), j)
            j! = j + 1
        }
        round! = round + 1
    }
    return m.len()
}

fun main() -> i32 {
    t0 := time_now_ms()
    a := bench_arith()
    t1 := time_now_ms()
    print("A arith      : " + (t1 - t0) + " ms (sum=" + a + ")")

    t2 := time_now_ms()
    b := bench_objects()
    t3 := time_now_ms()
    print("B objects    : " + (t3 - t2) + " ms (acc=" + b + ")")

    t4 := time_now_ms()
    c := bench_strings()
    t5 := time_now_ms()
    print("C strings    : " + (t5 - t4) + " ms (map_size=" + c + ")")

    print("L total      : " + (t5 - t0) + " ms")
    return 0
}
