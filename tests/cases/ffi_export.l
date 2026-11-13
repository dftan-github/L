// C-ABI exports for foreign callers (Ruby Fiddle / Python ctypes / C++).

pub fun add_numbers(i32 a, i32 b) -> i32 {
    return a + b
}

class Counter {
    i32 val
    fun init(i32 v) {
        val = v
    }
    pub fun increment(i32 step) {
        val = val + step
    }
    pub fun get_val() -> i32 {
        return val
    }
}

// Opaque-pointer surface: Class* travels as `ptr` across the FFI boundary.
pub fun counter_create(i32 init_val) -> ptr {
    c := heap Counter(init_val)
    return c as ptr
}

pub fun counter_increment(ptr c_ptr, i32 step) {
    c := c_ptr as Counter
    c.increment(step)
}

pub fun counter_get(ptr c_ptr) -> i32 {
    c := c_ptr as Counter
    return c.get_val()
}

pub fun counter_destroy(ptr c_ptr) {
    l_free(c_ptr)
}
