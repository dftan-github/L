// L vs C++ benchmark suite (native .exe, -O3). Native codegen only — never
// `l eval` or the comptime interpreter.
//
//   A dense float arithmetic   1e8 iterations of sum += i * 2.5
//   B object create + release  1e6 `heap Point` objects, reclaimed by auto-free
//   C string map               1e6 short keys inserted, then 1e6 lookups
//
// Build: l -O3 -o bench_l.exe stdlib/*.l benchmarks/bench_l.l

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

// A. Dense float arithmetic: 1e8 iterations. Float addition is not
// reassociable, so this survives -O3 as a real loop (an integer accumulator
// would be folded into one polynomial by both compilers).
fun bench_arith() -> f64 {
    f64 sum = 0.0
    i32 i = 0
    while i < 100000000 {
        sum = sum + i * 2.5
        i = i + 1
    }
    return sum
}

// B. Object create + release: 1e6 two-field `heap` objects, one per iteration.
// Each object is a real `l_alloc`; nothing is freed by hand, so the whole batch
// is reclaimed by the compiler's auto-free injection at scope exit.
fun bench_objects() -> i64 {
    i64 acc = 0
    i32 i = 0
    while i < 1000000 {
        p := heap Point(i, i + 1)
        acc = acc + p.x + p.y
        i = i + 1
    }
    return acc
}

// C. String map: build 1e6 short keys, insert every one, then look up every one.
// The map must actually hold 1e6 distinct entries, so the key space is 1e6 wide.
fun bench_strings() -> i32 {
    keys := Array<String>()
    i32 i = 0
    while i < 1000000 {
        keys.push("k" + i)
        i = i + 1
    }
    m := Map<i32>()
    i = 0
    while i < 1000000 {
        m.put(keys.get(i), i)
        i = i + 1
    }
    // Lookups: sum the values back so the reads cannot be dropped.
    i64 hits = 0
    i = 0
    while i < 1000000 {
        v := m.get(keys.get(i))
        if v.is_some() {
            hits = hits + 1
        }
        i = i + 1
    }
    print("C lookups hit: " + hits)
    return m.len()
}

fun main() -> i32 {
    t0 := time_now_ms()
    a := bench_arith()
    t1 := time_now_ms()
    print("A arith   : " + (t1 - t0) + " ms")
    print("  sum=" + a)

    t2 := time_now_ms()
    b := bench_objects()
    t3 := time_now_ms()
    print("B objects : " + (t3 - t2) + " ms")
    print("  acc=" + b)

    t4 := time_now_ms()
    c := bench_strings()
    t5 := time_now_ms()
    print("C strings : " + (t5 - t4) + " ms")
    print("  map_size=" + c)

    print("L total   : " + (t5 - t0) + " ms")
    return 0
}
