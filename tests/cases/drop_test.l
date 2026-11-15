import "stdlib/map.l"

// --- custom Drop hook ---------------------------------------------------
// `drop()` must run before the object memory is released, so it can still
// observe (and release) whatever the object owns.
i32 drop_calls = 0

class Tracker {
    i32 id
    fun init(i32 v) {
        id = v
    }
    pub fun drop() {
        drop_calls = drop_calls + 1
        print("drop " + id)
    }
}

// Scoped helper: the local goes out of scope on return, which is what triggers
// the auto-free path (and therefore drop).
fun scoped(i32 v) {
    t := Tracker(v)
    print("inside " + t.id)
}

// --- Map leak ----------------------------------------------------------
// A Map owns a heap copy of every key plus three buffers. Dropping it must
// release all of them, so the allocation counter returns to its start value.
fun map_churn() {
    for i in 0..200 {
        m := Map<i32>()
        m.put("alpha", i)
        m.put("beta", i * 2)
        m.put("gamma", i * 3)
        print_nothing := m.len()
        if print_nothing < 0 {
            print("unreachable")
        }
    }
}

fun main() -> i32 {
    // 1. Custom drop hook.
    scoped(7)
    print(drop_calls)

    // Nested scope: drop fires when the then-body ends, not at function exit.
    if drop_calls == 1 {
        inner := Tracker(9)
        print("block " + inner.id)
    }
    print(drop_calls)

    // 2. Map cleanup: allocation count must return to baseline.
    before := l_get_alloc_count()
    map_churn()
    after := l_get_alloc_count()
    print(after - before)

    // 3. A Map that is still alive keeps working (drop must not fire early).
    keep := Map<String>()
    keep.put("k", "v")
    print(keep.get("k") ?? "missing")
    return 0
}
