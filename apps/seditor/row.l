import "stdlib/memory.l"
import "stdlib/io.l"
import "stdlib/string.l"
import "stdlib/array.l"
import "apps/seditor/tree.l"

// One outline row. `handle` is the raw Node pointer so a click can toggle the
// fold state on the real node (a `Node` value here would be a copy).
pub class Row {
    i32 depth
    String label
    i32 line_no
    ptr handle
    i32 collapsed
    i32 has_kids

    fun init(i32 d, String l, i32 n, ptr h, i32 c, i32 k) {
        depth = d
        label = l
        line_no = n
        handle = h
        collapsed = c
        has_kids = k
    }
}

pub fun make_row(i32 depth, String label, i32 line_no, ptr handle, i32 collapsed, i32 has_kids) -> Row {
    r := heap Row(depth, label, line_no, handle, collapsed, has_kids)
    return r
}
