// Dynamic array — Layer 2 stdlib in L, heap via Layer 1 C runtime.
// pop / remove hand back Optional<T>, so optional.l comes along automatically.
import "../stdlib/optional.l"

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

    // --- Basic operations -------------------------------------------------

    // Out-of-range indices are not a crash: they yield an empty Optional.
    pub fun pop() -> Optional<T> {
        if length <= 0 {
            return Optional<T>()
        }
        length = length - 1
        return Optional<T>(true, get(length))
    }

    // `index` may be `length` (append). Out of range → no-op.
    pub fun insert(i32 index, T val) {
        bool ok = index >= 0 && index <= length
        if ok {
            push(val)             // grow + reserve the trailing slot
            i32 i = length - 1
            while i > index {
                set(i, get(i - 1))
                i = i - 1
            }
            set(index, val)
        }
    }

    // Removes and returns the element. Out of range → empty Optional.
    pub fun remove(i32 index) -> Optional<T> {
        if index < 0 || index >= length {
            return Optional<T>()
        }
        Optional<T> hit = Optional<T>(true, get(index))
        i32 i = index
        while i < length - 1 {
            set(i, get(i + 1))
            i = i + 1
        }
        length = length - 1
        return hit
    }

    // In-place reversal: swap both ends inward.
    pub fun reverse() {
        i32 i = 0
        i32 j = length - 1
        while i < j {
            T tmp = get(i)
            set(i, get(j))
            set(j, tmp)
            i = i + 1
            j = j - 1
        }
    }

    pub fun contains(T val) -> bool {
        i32 i = 0
        while i < length {
            if get(i) == val {
                return true
            }
            i = i + 1
        }
        return false
    }

    // Returns the first matching index, or -1 when absent.
    pub fun index_of(T val) -> i32 {
        i32 i = 0
        while i < length {
            if get(i) == val {
                return i
            }
            i = i + 1
        }
        return -1
    }

    // Drops every element but keeps the allocation for reuse.
    pub fun clear() {
        length = 0
    }
}
