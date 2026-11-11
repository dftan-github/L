// Optional<T> — null-safe presence wrapper (no implicit nil).
pub class Optional<T> {
    bool has_value_
    T value_

    fun is_some() -> bool {
        return has_value_
    }

    fun unwrap() -> T {
        return value_
    }

    fun unwrap_or(T default_val) -> T {
        if has_value_ {
            return value_
        }
        return default_val
    }
}
