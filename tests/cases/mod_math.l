// Module-scope demo: `pub fun` is exported as `mod_math::add`; the
// non-pub `secret` stays private to this file.
pub fun add(i32 a, i32 b) -> i32 {
    return a + b
}

fun secret() -> i32 {
    return 42
}

// A module can call its own private helper directly.
pub fun via_secret() -> i32 {
    return secret() + 1
}
