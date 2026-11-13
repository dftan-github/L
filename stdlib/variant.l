// Dynamic Variant — tagged heap cell (type_tag + data).
// Tags: 1 = i32, 2 = String (c_str ptr), 3 = f64, 4 = bool (as i32 0/1), 5 = Array (reserved).
// Factories are free functions; call as Variant.from_i32(...) / Variant.from_f64(...).

pub class Variant {
    i32 type_tag
    ptr data

    fun init(i32 tag, ptr d) {
        type_tag = tag
        data = d
    }

    pub fun is_i32() -> bool {
        return type_tag == 1
    }

    pub fun is_string() -> bool {
        return type_tag == 2
    }

    pub fun is_f64() -> bool {
        return type_tag == 3
    }

    pub fun is_bool() -> bool {
        return type_tag == 4
    }

    pub fun as_i32() -> Optional<i32> {
        if type_tag == 1 {
            i32 val = l_load_i32(data)
            return Optional<i32>(true, val)
        }
        return Optional<i32>(false, 0)
    }

    pub fun as_string() -> Optional<String> {
        if type_tag == 2 {
            ptr s = l_load_ptr(data)
            return Optional<String>(true, String(s))
        }
        return Optional<String>(false, String(""))
    }

    pub fun as_f64() -> Optional<f64> {
        if type_tag == 3 {
            f64 val = l_load_f64(data)
            return Optional<f64>(true, val)
        }
        return Optional<f64>(false, 0.0)
    }

    pub fun as_bool() -> Optional<bool> {
        if type_tag == 4 {
            i32 n = l_load_i32(data)
            if n != 0 {
                return Optional<bool>(true, true)
            }
            return Optional<bool>(true, false)
        }
        return Optional<bool>(false, false)
    }
}

pub fun from_i32(i32 v) -> Variant {
    ptr p = l_alloc(sizeof(i32))
    l_store_i32(p, v)
    return Variant(1, p)
}

pub fun from_string(String v) -> Variant {
    ptr p = l_alloc(sizeof(ptr))
    l_store_ptr(p, v.c_str())
    return Variant(2, p)
}

pub fun from_f64(f64 v) -> Variant {
    ptr p = l_alloc(sizeof(f64))
    l_store_f64(p, v)
    return Variant(3, p)
}

pub fun from_bool(bool v) -> Variant {
    // Store as i32 0/1 in a heap cell (stable ABI across platforms).
    ptr p = l_alloc(sizeof(i32))
    i32 n = 0
    if v {
        n = 1
    }
    l_store_i32(p, n)
    return Variant(4, p)
}
