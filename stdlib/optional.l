// Optional<T> — null-safe presence wrapper (no implicit nil).
pub class Optional<T> {
    bool has_value
    T value

    fun is_some() -> bool {
        return has_value
    }

    fun unwrap() -> T {
        return value
    }

    fun unwrap_or(T default_val) -> T {
        if has_value {
            return value
        }
        return default_val
    }
}
