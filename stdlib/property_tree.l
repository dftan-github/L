// PropertyTree — a tree of Variant values and named child nodes.
//
// NODES HAVE REFERENCE SEMANTICS. A `PropertyTree` value is a single heap-block
// address (`block`), so copying the handle never copies node state: a node
// attached to a parent and later mutated through the caller's handle is the
// same node.
//
// Block layout (32 bytes, created by `init`):
//   off  0 : i32  has_val  (0 = unset)
//   off  4 : i32  type_tag (Variant tag)
//   off  8 : ptr  data     (Variant payload cell)
//   off 16 : ptr  kids     (a `Map<ptr>`: name → child block address)
//   off 24 : i32  kids_installed (0 = lazily created)
//
// `sizeof(PropertyTree)` is one pointer, so the type is finite no matter how
// deep the tree goes.
//
// Self-contained: importing this file alone pulls in everything it needs, so a
// user file only writes `import "stdlib/property_tree.l"`.
import "stdlib/memory.l"
import "stdlib/io.l"
import "stdlib/array.l"
import "stdlib/string.l"
import "stdlib/optional.l"
import "stdlib/map.l"
import "stdlib/variant.l"

pub fun indent(i32 depth) -> String {
    String out = String("")
    i32 i = 0
    while i < depth {
        out = out + "  "
        i = i + 1
    }
    return out
}

// Splits `a.b.c` on '.' — empty segments are dropped (`slice` takes a length).
pub fun split_dots(String path) -> Array<String> {
    Array<String> out = Array<String>()
    i32 start = 0
    i32 i = 0
    while i < path.length() {
        if path.char_at(i) == 46 {
            if i > start {
                out.push(path.slice(start, i - start))
            }
            start = i + 1
        }
        i = i + 1
    }
    if start < path.length() {
        out.push(path.slice(start, path.length() - start))
    }
    return out
}

// Renders a Variant payload without relying on the print(Variant) sugar.
pub fun describe(Variant v) -> String {
    if v.is_i32() {
        return String.from(v.as_i32().unwrap())
    }
    if v.is_string() {
        return v.as_string().unwrap()
    }
    if v.is_f64() {
        return str_from_f64(v.as_f64().unwrap())
    }
    if v.is_bool() {
        return str_from_bool(v.as_bool().unwrap())
    }
    return "<variant>"
}

pub class PropertyTree {
    ptr block

    pub fun init() {
        ptr b = l_alloc(32)
        l_store_i32_at(b, 0, 0)          // has_val = false
        l_store_i32_at(b, 4, 0)          // type_tag = 0
        ptr cell = l_alloc(16)
        l_store_ptr_at(b, 8, cell)       // payload cell (stable address)
        l_store_ptr_at(b, 16, l_null_ptr())  // child index (created lazily)
        l_store_i32_at(b, 24, 0)         // child index installed flag
        block = b
    }

    // Wraps an existing block address. The freshly allocated placeholder block
    // from `PropertyTree()` is released first, so this does not leak.
    pub static fun from_block(ptr b) -> PropertyTree {
        n := PropertyTree()
        ptr scratch = n.block
        n.block = b
        l_free(l_load_ptr_at(scratch, 8))
        l_free(scratch)
        return n
    }

    // --- internals --------------------------------------------------------

    // Returns the raw child-index pointer; `Map<ptr>` is only materialized at
    // the call site (returning a class by value from a method is not supported).
    fun kids() -> ptr {
        if l_load_i32_at(block, 24) == 0 {
            m := heap Map<ptr>()
            ptr mp = m as ptr
            l_store_ptr_at(block, 16, mp)
            l_store_i32_at(block, 24, 1)
            return mp
        }
        return l_load_ptr_at(block, 16)
    }

    // --- value slot -------------------------------------------------------

    pub fun set_val(Variant v) {
        l_store_i32_at(block, 0, 1)
        l_store_i32_at(block, 4, v.type_tag)
        l_store_ptr_at(block, 8, v.data)
    }

    pub fun has_value() -> bool {
        return l_load_i32_at(block, 0) != 0
    }

    pub fun clear_val() {
        l_store_i32_at(block, 0, 0)
    }

    pub fun get_val() -> Optional<Variant> {
        if l_load_i32_at(block, 0) == 0 {
            return Optional<Variant>()
        }
        i32 tag = l_load_i32_at(block, 4)
        ptr data = l_load_ptr_at(block, 8)
        return Optional<Variant>(true, Variant(tag, data))
    }

    // --- children ---------------------------------------------------------

    // Inserts or replaces the child stored under `key`. Only the child's block
    // address is stored, so later mutations through the caller's handle show up.
    pub fun add_child(String key, PropertyTree node) {
        k := kids() as Map<ptr>
        k.put(key, node.block)
    }

    pub fun get_child(String key) -> Optional<PropertyTree> {
        k := kids() as Map<ptr>
        found := k.get(key)
        if !found.is_some() {
            return Optional<PropertyTree>()
        }
        return Optional<PropertyTree>(true, PropertyTree.from_block(found.unwrap()))
    }

    pub fun has_child(String key) -> bool {
        k := kids() as Map<ptr>
        return k.get(key).is_some()
    }

    pub fun child_count() -> i32 {
        k := kids() as Map<ptr>
        return k.len()
    }

    pub fun child_keys() -> Array<String> {
        k := kids() as Map<ptr>
        return k.keys()
    }

    pub fun is_leaf() -> bool {
        k := kids() as Map<ptr>
        return k.len() == 0
    }

    // --- dotted-path access ----------------------------------------------

    // Walks `a.b.c` from this node. An empty path yields this node; a missing
    // segment yields an empty Optional.
    pub fun get_path(String path) -> Optional<PropertyTree> {
        if path.length() == 0 {
            return Optional<PropertyTree>(true, this)
        }
        parts := split_dots(path)
        cur := this
        i32 i = 0
        while i < parts.len() {
            seg := parts.get(i)
            nxt := cur.get_child(seg)
            if !nxt.is_some() {
                return Optional<PropertyTree>()
            }
            cur = nxt.unwrap()
            i = i + 1
        }
        return Optional<PropertyTree>(true, cur)
    }

    // Depth-first walk; `depth` is the indentation level.
    pub fun dump(i32 depth) {
        pad := indent(depth)
        v := get_val()
        if v.is_some() {
            line := pad + "= " + describe(v.unwrap())
            print(line + "\n")
        } else {
            print(pad + "(empty)\n")
        }
        names := child_keys()
        i32 i = 0
        while i < names.len() {
            key := names.get(i)
            idx := pad + key + ":\n"
            print(idx)
            child := get_child(key).unwrap()
            nxt_depth := depth + 1
            child.dump(nxt_depth)
            i = i + 1
        }
    }
}

// Wraps an existing block address as a handle (no allocation).



