// Hash map with String keys — Layer 2 stdlib in L.
// Open addressing; parallel keys_ / values_ / occupied_ buffers.
// Note: ASI does not insert ';' after ']' or bool literals — keep them explicit.
pub class Map<V> {
    ptr keys
    ptr values
    ptr occupied
    i32 size
    i32 capacity

    fun init() {
        capacity = 8
        size = 0
        keys = l_alloc(capacity * sizeof(ptr))
        values = l_alloc(capacity * sizeof(V))
        occupied = l_alloc(capacity * sizeof(bool))
        i32 i = 0
        while i < capacity {
            occupied[i] = false;
            i = i + 1
        }
    }

    fun grow() {
        i32 old_cap = capacity
        ptr old_keys = keys
        ptr old_values = values
        ptr old_occupied = occupied

        capacity = capacity * 2
        keys = l_alloc(capacity * sizeof(ptr))
        values = l_alloc(capacity * sizeof(V))
        occupied = l_alloc(capacity * sizeof(bool))
        size = 0

        i32 j = 0
        while j < capacity {
            occupied[j] = false;
            j = j + 1
        }

        i32 i = 0
        while i < old_cap {
            if old_occupied[i] {
                insert_raw(old_keys[i], old_values[i])
            }
            i = i + 1
        }

        l_free(old_keys)
        l_free(old_values)
        l_free(old_occupied)
    }

    fun insert_raw(ptr key_cstr, V val) {
        i64 h = l_hash_string(key_cstr)
        i32 idx = l_hash_to_bucket(h, capacity)
        bool placed = false;
        while placed == false {
            if occupied[idx] {
                if l_str_eq(keys[idx], key_cstr) != 0 {
                    values[idx] = val;
                    placed = true;
                } else {
                    idx = idx + 1
                    if idx >= capacity {
                        idx = 0
                    }
                }
            } else {
                keys[idx] = key_cstr;
                values[idx] = val;
                occupied[idx] = true;
                size = size + 1
                placed = true;
            }
        }
    }

    pub fun put(String key, V val) {
        if size * 2 >= capacity {
            grow()
        }
        insert_raw(key.c_str(), val)
    }

    pub fun get(String key) -> Optional<V> {
        ptr key_cstr = key.c_str()
        i64 h = l_hash_string(key_cstr)
        i32 idx = l_hash_to_bucket(h, capacity)
        i32 start = idx
        while occupied[idx] {
            if l_str_eq(keys[idx], key_cstr) != 0 {
                return Optional<V>(true, values[idx]);
            }
            idx = idx + 1
            if idx >= capacity {
                idx = 0
            }
            if idx == start {
                return Optional<V>()
            }
        }
        return Optional<V>()
    }

    pub fun len() -> i32 {
        return size
    }

    // Snapshot of keys currently stored (order undefined; copies C strings).
    pub fun keys() -> Array<String> {
        arr := Array<String>()
        i! := 0
        while i < capacity {
            if occupied[i] {
                tmp := String(keys[i])
                arr.push(tmp.slice(0, tmp.length()))
            }
            i! = i + 1
        }
        return arr
    }
}
