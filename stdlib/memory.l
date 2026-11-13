// Layer 1 C runtime heap API (linked from runtime/memory.c).
extern module c_memory {
    fun l_alloc(i64 size) -> ptr
    fun l_realloc(ptr p, i64 size) -> ptr
    fun l_free(ptr p) -> void
    fun l_get_alloc_count() -> i32
    fun l_arena_new(i64 size) -> ptr
    fun l_arena_alloc(ptr pool, i64 size) -> ptr
    fun l_arena_free(ptr pool) -> void
    fun l_store_i32(ptr p, i32 v) -> void
    fun l_load_i32(ptr p) -> i32
    fun l_store_f64(ptr p, f64 v) -> void
    fun l_load_f64(ptr p) -> f64
    fun l_store_u8(ptr p, i32 index, i32 v) -> void
    fun l_store_ptr(ptr p, ptr v) -> void
    fun l_load_ptr(ptr p) -> ptr
}
