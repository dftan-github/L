// "Free-for-all" L benchmark (-O3): every trick allowed, fastest wins.
//
//   A string concat   1e6 short appends into one pre-sized byte buffer
//   B small objects   1e6 allocations from the built-in arena
//   C float math      1e7 sin/cos evaluations accumulated
//
// The strict-semantics suite lives in bench_l.l; this file is the
// "squeeze everything" counterpart of bench_freeforall_cpp.cpp.

import "../stdlib/string.l"
import "../stdlib/optional.l"
import "../stdlib/array.l"
import "../stdlib/map.l"
import "../stdlib/time.l"

extern module c_math {
    fun sin(f64 x) -> f64
    fun cos(f64 x) -> f64
}

class Point {
    i32 x
    i32 y
    fun init(i32 a, i32 b) {
        x = a
        y = b
    }
}

// A. String concatenation: 1e6 x "k<i>," written into one 16 MiB byte buffer.
// Zero per-append allocation: decimal digits are emitted with raw byte stores,
// which is the cheapest shape this workload can take in any language.
fun bench_strings() -> i32 {
    ptr buf = l_alloc(16777216)
    i32 pos = 0
    i32 i = 0
    while i < 1000000 {
        l_store_u8(buf, pos, 107)  // 'k'
        pos = pos + 1

        // Decimal digits of `i`, most significant first (i is not modified).
        i32 scale = 1
        while scale <= i / 10 {
            scale = scale * 10
        }
        i32 rest = i
        while scale > 0 {
            d := rest / scale
            l_store_u8(buf, pos, 48 + d)
            pos = pos + 1
            rest = rest - d * scale
            scale = scale / 10
        }

        l_store_u8(buf, pos, 44)  // ','
        pos = pos + 1
        i = i + 1
    }
    l_free(buf)
    return pos
}

// B. Small-object allocation: 1e6 objects from one built-in arena.
fun bench_objects() -> i64 {
    i64 acc = 0
    a := arena(33554432)
    i32 i = 0
    while i < 1000000 {
        p := a.new(Point(i, i + 1))
        acc = acc + p.x + p.y
        i = i + 1
    }
    return acc
}

// C. Float math: 1e7 sin/cos evaluations — same libm as the C++ side.
fun bench_math() -> f64 {
    f64 s = 0.0
    i32 i = 0
    while i < 10000000 {
        f64 t = i * 0.001
        s = s + sin(t) * cos(t)
        i = i + 1
    }
    return s
}

fun main() -> i32 {
    t0 := time_now_ms()
    a := bench_strings()
    t1 := time_now_ms()
    print("A strings : " + (t1 - t0) + " ms")
    print("  bytes=" + a)

    t2 := time_now_ms()
    b := bench_objects()
    t3 := time_now_ms()
    print("B objects : " + (t3 - t2) + " ms")
    print("  acc=" + b)

    t4 := time_now_ms()
    c := bench_math()
    t5 := time_now_ms()
    print("C math    : " + (t5 - t4) + " ms")
    print("  sum=" + c)

    print("L total   : " + (t5 - t0) + " ms")
    return 0
}
