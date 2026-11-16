// Hash map with String keys — Layer 2 stdlib in L.
// Open addressing; parallel keys_ / values_ / occupied_ buffers.
// Note: ASI does not insert ';' after ']' or bool literals — keep them explicit.
//
// Ownership: each bucket that is `occupied` owns one heap C-string returned by
// `String.slice` (i.e. a copy, never a borrowed `c_str()`). That copy is freed
// on overwrite, on `remove`, and when the whole map is torn down.
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

    // Owning heap copy of a key. Never store a borrowed `String.c_str()`.
    fun copy_key(String key) -> ptr {
        return key.slice(0, key.length()).c_str()
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

        // Rehash owned key pointers: ownership moves to the new table, so the
        // old buffers are released without touching the strings themselves.
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

    // `key_cstr` is a lookup-only pointer (never retained).
    fun find_slot(ptr key_cstr) -> i32 {
        i64 h = l_hash_string(key_cstr)
        i32 idx = l_hash_to_bucket(h, capacity)
        i32 guard = 0
        while occupied[idx] && guard < capacity {
            if l_str_eq(keys[idx], key_cstr) != 0 {
                return idx
            }
            idx = idx + 1
            if idx >= capacity {
                idx = 0
            }
            guard = guard + 1
        }
        return -1
    }

    // `key_cstr` is a fresh lookup pointer: the slot stores its own copy.
    fun insert_raw(ptr key_cstr, V val) {
        i64 h = l_hash_string(key_cstr)
        i32 idx = l_hash_to_bucket(h, capacity)
        bool placed = false;
        while !placed {
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
                keys[idx] = copy_key(String(key_cstr));
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
        i32 idx = find_slot(key.c_str())
        if idx < 0 {
            return Optional<V>()
        }
        return Optional<V>(true, values[idx])
    }

    pub fun contains(String key) -> bool {
        return find_slot(key.c_str()) >= 0
    }

    // Deletes a key and hands its bucket back to the probe chain.
    //
    // Open addressing cannot simply clear a slot: later keys whose probe path
    // crossed it would become unreachable. The classic fix is backward-shift
    // deletion — repeatedly pull the next cluster element into the hole while
    // its ideal bucket is not "between" the hole and itself.
    pub fun remove(String key) {
        i32 idx = find_slot(key.c_str())
        bool found = idx >= 0
        if found {
            l_free(keys[idx])
            occupied[idx] = false;
            size = size - 1
            i32 hole = idx

            i32 next = hole + 1
            if next >= capacity {
                next = 0
            }
            while occupied[next] {
                i32 ideal = l_hash_to_bucket(l_hash_string(keys[next]), capacity)
                bool movable = false;
                if hole <= next {
                    if ideal > hole && ideal <= next {
                        movable = true;
                    }
                } else {
                    // Wrapped region: the home slot lies outside [hole, next].
                    if ideal > hole || ideal <= next {
                        movable = true;
                    }
                }
                if !movable {
                    break
                }
                keys[hole] = keys[next]
                values[hole] = values[next]
                occupied[hole] = true;
                occupied[next] = false;
                hole = next
                next = hole + 1
                if next >= capacity {
                    next = 0
                }
            }
        }
    }

    pub fun len() -> i32 {
        return size
    }

    // Class destructor: releases every owned key string plus the three buffers.
    // Called by codegen's auto-free path just before `l_free(this)`.
    pub fun drop() {
        i32 i = 0
        while i < capacity {
            if occupied[i] {
                l_free(keys[i])
            }
            i = i + 1
        }
        l_free(keys)
        l_free(values)
        l_free(occupied)
        size = 0
        capacity = 0
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
