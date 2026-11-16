// CLI args + environment helpers (C symbols in stdlib/io.l; impl in runtime/sys.c).

pub fun get_args() -> Array<String> {
    arr := Array<String>()
    n := l_get_arg_count()
    i! := 0
    while i < n {
        arr.push(String(l_get_arg(i)))
        i! = i + 1
    }
    return arr
}

pub fun get_env(String name) -> Optional<String> {
    p := l_get_env(name.c_str())
    if l_ptr_is_null(p) != 0 {
        return Optional<String>()
    }
    // Copy — getenv pointer must not be freed / must outlive String use.
    tmp := String(p)
    out := tmp.slice(0, tmp.length())
    return Optional<String>(true, out)
}

// Set an environment variable for this process (and anything it spawns).
// Returns false when the platform rejects the name or value.
pub fun set_env(String name, String value) -> bool {
    return l_set_env(name.c_str(), value.c_str()) == 0
}
