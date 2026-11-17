// SEditor shared string table.
//
// Every distinct source line is stored once and referred to by a 1-based tag
// (0 = empty sentinel). This keeps the Node tree from copying each line twice
// and turns `join` into a plain tag replay in file order.
//
// Lookup is a linear scan over an Array<String>: the table only holds the
// distinct lines of the OPEN file (hundreds, not millions), so the scan cost is
// paid once per line at build time.
//
// Constraints this file works around, both verified against the current compiler:
//  * A global `ptr` cannot carry an initializer ([globals]), but a global CLASS
//    instance can ([global-class]). So the table lives inside `g_tab`.
//  * A global class instance supports field reads (`g_tab.values`) but NOT
//    method calls (`g_tab.at(...)` -> "unknown object in method call"). So all
//    behaviour lives in free functions below; `StringTab` is pure storage.
//  * `Array<T>` index syntax outside a generic body cannot infer the element
//    type ("cannot determine element type for pointer index"). Reads therefore
//    go through `values.get(i)`, called from code that knows the element type.

import "stdlib/memory.l"
import "stdlib/io.l"
import "stdlib/string.l"
import "stdlib/array.l"

pub class StringTab {
    Array<String> values

    fun init() {
        values = Array<String>()
        values.push(String(""))
    }
}

StringTab g_tab = StringTab()

// Returns the tag for `s`, inserting it when new. Tags are 1-based.
pub fun string_tab_intern(String s) -> i32 {
    i32 n = g_tab.values.len()
    i32 i = 1
    while i < n {
        if l_str_eq(g_tab.values.get(i).c_str(), s.c_str()) != 0 {
            return i
        }
        i = i + 1
    }
    // Own a private copy: callers pass slices of buffers that may be freed as
    // soon as they return (see `build_path`), and the table outlives them.
    // `l_str_substr` allocates a fresh buffer, so the copy is independent.
    i32 len = s.length()
    ptr copy = l_str_substr(s.c_str(), 0, len)
    g_tab.values.push(String(copy))
    return g_tab.values.len() - 1
}

pub fun string_tab_get(i32 tag) -> String {
    if tag <= 0 || tag >= g_tab.values.len() {
        empty := String("")
        return empty
    }
    return g_tab.values.get(tag)
}
