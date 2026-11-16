// Auto-free vs explicit l_free: the object must be released exactly once.
//
// A `heap` allocation is registered for end-of-scope auto-free. When the program
// also takes the release into its own hands (`l_free(p)` or `defer l_free(p)`),
// Codegen must drop the auto-free entry — otherwise the pointer is freed twice
// and the CRT aborts with a heap-corruption code (0xC0000374).
//
// The allocation delta is measured without any string building in between, so a
// non-zero value means the Point itself was never (or doubly) released.

class Point {
    i32 x
    i32 y
    fun init(i32 a, i32 b) {
        x = a
        y = b
    }
}

// Case 1: plain `l_free(p)` on a heap local.
fun free_by_statement() -> i32 {
    before := l_get_alloc_count()
    p := heap Point(1, 2)
    n := p.x + p.y
    l_free(p)
    return l_get_alloc_count() - before
}

// Case 2: `defer l_free(p)` — the defer runs at scope exit, auto-free must not.
fun free_by_defer() -> i32 {
    before := l_get_alloc_count()
    p := heap Point(3, 4)
    n := p.x + p.y
    defer l_free(p)
    after_use := l_get_alloc_count() - before
    return after_use - 1
}

// Case 3: heap local released by hand inside a nested scope.
fun free_in_nested_block() -> i32 {
    before := l_get_alloc_count()
    if before >= 0 {
        q := heap Point(5, 6)
        l_free(q)
    }
    return l_get_alloc_count() - before
}

fun main() -> i32 {
    a := free_by_statement()
    b := free_by_defer()
    c := free_in_nested_block()

    print("statement delta: " + a)
    print("defer delta: " + b)
    print("nested delta: " + c)
    if a + b + c == 0 {
        print("no double free, no leak")
    } else {
        print("LEAK DETECTED")
    }
    return 0
}
