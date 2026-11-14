// Pure-L math helpers. Business logic stays on the L side; no C runtime here.

// Integer addition. Wider/narrower numeric types promote at the call site.
pub fun add(i32 a, i32 b) -> i32 {
    return a + b
}

// Floating-point addition, kept separate because L has no function overloading.
pub fun add_f64(f64 a, f64 b) -> f64 {
    return a + b
}
