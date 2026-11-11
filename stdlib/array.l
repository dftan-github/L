// Dynamic array — Layer 2 stdlib in L, heap via Layer 1 C runtime.
pub class Array<T> {
    ptr data_
    i32 length_
    i32 capacity_

    fun push(T item) {
        if length_ >= capacity_ {
            i32 new_cap = capacity_ * 2
            if new_cap == 0 {
                new_cap = 4
            }
            ptr new_data = l_realloc(data_, new_cap * sizeof(T))
            data_ = new_data
            capacity_ = new_cap
        }
        data_[length_] = item
        length_ = length_ + 1
    }

    fun get(i32 index) -> T {
        // ASI does not trigger after ']'; keep explicit ';'
        return data_[index];
    }
}
