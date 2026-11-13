// Dynamic array — Layer 2 stdlib in L, heap via Layer 1 C runtime.
pub class Array<T> {
    ptr data
    i32 length
    i32 capacity

    fun push(T item) {
        if length >= capacity {
            i32 new_cap = capacity * 2
            if new_cap == 0 {
                new_cap = 4
            }
            ptr new_data = l_realloc(data, new_cap * sizeof(T))
            data = new_data
            capacity = new_cap
        }
        data[length] = item
        length = length + 1
    }

    fun get(i32 index) -> T {
        return data[index]
    }

    pub fun set(i32 index, T item) {
        data[index] = item
    }

    pub fun len() -> i32 {
        return length
    }

    pub fun each(fun(T) action) {
        i32 i = 0
        while i < length {
            action(get(i))
            i = i + 1
        }
    }
}
