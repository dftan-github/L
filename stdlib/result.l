// Result<T, E> — explicit success / error (no exceptions).
// Phase 1: E is typically String (error message).
pub class Result<T, E> {
    bool is_ok
    T ok_val
    E err_val

    pub fun is_ok() -> bool {
        return is_ok
    }

    pub fun unwrap() -> T {
        return ok_val
    }

    pub fun unwrap_err() -> E {
        return err_val
    }
}
