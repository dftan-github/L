// Layer 1 C runtime thread / mutex API (runtime/thread.c).
extern module c_thread {
    fun l_mutex_init() -> ptr
    fun l_mutex_lock(ptr m) -> void
    fun l_mutex_unlock(ptr m) -> void
    fun l_mutex_free(ptr m) -> void
    fun l_mutex_global() -> ptr
    fun l_spawn(ptr fn, ptr arg) -> ptr
    fun l_thread_join(ptr handle) -> void
    fun l_sleep_ms(i32 ms) -> void
    fun l_get_lock_count() -> i64
    fun l_get_unlock_count() -> i64
    fun l_reset_lock_counts() -> void
    fun l_atomic_add_i32(ptr p, i32 val) -> i32
    fun l_atomic_load_i32(ptr p) -> i32
    fun l_atomic_store_i32(ptr p, i32 val) -> void
}

// Wait for a spawn handle (opaque ptr from `h := spawn { ... }`).
pub fun join(ptr handle) -> void {
    l_thread_join(handle)
}
