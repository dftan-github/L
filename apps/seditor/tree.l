// SEditor source tree.
//
// A node is EITHER a category (KIND_CLASS / KIND_FUNC / ...) OR a source line
// (KIND_LINE). Node text is stored as tags into the shared string table
// (apps/seditor/strtab.l): replaying tags in file order reproduces the file.
//
// Ordering is the whole point, so children live in a flat heap array of
// pointers. Never use a Map here — Map key order is undefined (stdlib/map.l).
//
// WHY EVERYTHING HERE IS `ptr` — two L behaviours make class VALUES unusable
// for a mutable tree (both verified against the current compiler):
//  * `b = a` for a class is a bitwise COPY, not an alias. A helper taking
//    `Node n` therefore mutates the caller's copy and silently drops writes.
//  * `local as ptr` yields the address of the local's STACK SLOT, not of the
//    heap object. Inside a loop that slot is reused every iteration, so N
//    stored "handles" all point at one stack word and read back as the last
//    value written.
// So nodes are created as `ptr`, passed as `ptr`, and fields are read/written
// through `l_load_*_at` / `l_store_*_at` at the offsets below.

import "stdlib/memory.l"
import "stdlib/io.l"
import "stdlib/string.l"
import "stdlib/array.l"
import "stdlib/path.l"
import "stdlib/file.l"
import "stdlib/fs.l"
import "apps/seditor/strtab.l"

// --- node kinds ------------------------------------------------------------

const KIND_FILE = 0
const KIND_CLASS = 1
const KIND_FUNC = 2
const KIND_BLOCK = 3
const KIND_LINE = 4
const KIND_COMMENT = 5
const KIND_DIRECTIVE = 6
const KIND_DIRECTORY = 7

// Line separator replayed by `join`. A function, not a `const`: a global
// `String` constant is rejected ("const global class instances are not
// supported"), and silently degrading to "" would lose every newline.
fun newline() -> String {
    s := String("\n")
    return s
}

// Field order is part of the ABI — do not reorder.
pub class Node {
    i32 kind
    i32 line_no       // 1-based line this node starts at
    i32 entry_tag     // string-table tag for this node's own text
    ptr child_data    // flat array of Node* (ordered)
    i32 child_count
    i32 child_cap
    ptr line_data     // flat array of i32 string tags (ordered)
    i32 line_count
    i32 line_cap
    i32 collapsed     // UI state only: 1 hides every descendant row
    i32 ends_nl       // builder flag: 1 if the source ended with '\n'
}

// Byte offsets of the `Node` layout above (x64). Keep in sync with the class.
// (Trailing padding to 56 bytes keeps the allocation 8-byte aligned.)
const OFF_KIND = 0
const OFF_LINE_NO = 4
const OFF_ENTRY_TAG = 8
const OFF_CHILD_DATA = 16
const OFF_CHILD_COUNT = 24
const OFF_CHILD_CAP = 28
const OFF_LINE_DATA = 32
const OFF_LINE_COUNT = 40
const OFF_LINE_CAP = 44
const OFF_COLLAPSED = 48
const OFF_ENDS_NL = 52
const NODE_SIZE = 56

pub fun node_kind(ptr n) -> i32 {
    return l_load_i32_at(n, OFF_KIND)
}

pub fun node_line_no(ptr n) -> i32 {
    return l_load_i32_at(n, OFF_LINE_NO)
}

pub fun node_entry_tag(ptr n) -> i32 {
    return l_load_i32_at(n, OFF_ENTRY_TAG)
}

pub fun node_child_count(ptr n) -> i32 {
    return l_load_i32_at(n, OFF_CHILD_COUNT)
}

pub fun node_line_count(ptr n) -> i32 {
    return l_load_i32_at(n, OFF_LINE_COUNT)
}

// Allocates a node. Returns the raw handle so callers never round-trip
// through a `Node` value (which would copy, or take the wrong address).
pub fun make_node(i32 kind, i32 line_no, i32 entry_tag) -> ptr {
    ptr n = l_alloc(NODE_SIZE)
    if l_ptr_is_null(n) != 0 {
        return n
    }
    l_store_i32_at(n, OFF_KIND, kind)
    l_store_i32_at(n, OFF_LINE_NO, line_no)
    l_store_i32_at(n, OFF_ENTRY_TAG, entry_tag)
    l_store_ptr_at(n, OFF_CHILD_DATA, l_null_ptr())
    l_store_i32_at(n, OFF_CHILD_COUNT, 0)
    l_store_i32_at(n, OFF_CHILD_CAP, 0)
    l_store_ptr_at(n, OFF_LINE_DATA, l_null_ptr())
    l_store_i32_at(n, OFF_LINE_COUNT, 0)
    l_store_i32_at(n, OFF_LINE_CAP, 0)
    l_store_i32_at(n, OFF_COLLAPSED, 0)
    l_store_i32_at(n, OFF_ENDS_NL, 1)
    return n
}

pub fun node_ends_with_newline(ptr n) -> i32 {
    return l_load_i32_at(n, OFF_ENDS_NL)
}

pub fun set_ends_with_newline(ptr n, i32 v) -> void {
    l_store_i32_at(n, OFF_ENDS_NL, v)
}

// --- fold state (outline only; `join` ignores it) ---------------------------

pub fun node_collapsed(ptr n) -> i32 {
    return l_load_i32_at(n, OFF_COLLAPSED)
}

pub fun set_collapsed(ptr n, i32 v) -> void {
    l_store_i32_at(n, OFF_COLLAPSED, v)
}

// Sets the fold flag on every descendant. Recursive because children are a
// flat pointer array, not a level structure.
pub fun collapse_descendants(ptr n, i32 v) -> void {
    i32 kids = l_load_i32_at(n, OFF_CHILD_COUNT)
    i32 i = 0
    while i < kids {
        ptr c = child_at(n, i)
        set_collapsed(c, v)
        collapse_descendants(c, v)
        i = i + 1
    }
}

// Deepest nesting reachable from `n`; a leaf is depth 0.
pub fun subtree_depth(ptr n) -> i32 {
    i32 best = 0
    i32 kids = l_load_i32_at(n, OFF_CHILD_COUNT)
    i32 i = 0
    while i < kids {
        i32 d = subtree_depth(child_at(n, i)) + 1
        if d > best {
            best = d
        }
        i = i + 1
    }
    return best
}

// --- children --------------------------------------------------------------

fun grow_children(ptr n, i32 need) -> void {
    i32 cap = l_load_i32_at(n, OFF_CHILD_CAP)
    if need <= cap {
        return
    }
    if cap == 0 {
        cap = 4
    }
    while cap < need {
        cap = cap * 2
    }
    ptr fresh = l_alloc(cap * sizeof(ptr))
    if l_ptr_is_null(fresh) != 0 {
        return
    }
    ptr old = l_load_ptr_at(n, OFF_CHILD_DATA)
    i32 count = l_load_i32_at(n, OFF_CHILD_COUNT)
    i32 i = 0
    while i < count {
        l_store_ptr_at(fresh, i * 8, l_load_ptr_at(old, i * 8))
        i = i + 1
    }
    if l_ptr_is_null(old) == 0 {
        l_free(old)
    }
    l_store_ptr_at(n, OFF_CHILD_DATA, fresh)
    l_store_i32_at(n, OFF_CHILD_CAP, cap)
}

pub fun push_child(ptr parent, ptr child) -> void {
    i32 count = l_load_i32_at(parent, OFF_CHILD_COUNT)
    grow_children(parent, count + 1)
    ptr data = l_load_ptr_at(parent, OFF_CHILD_DATA)
    if l_ptr_is_null(data) != 0 {
        return
    }
    l_store_ptr_at(data, count * 8, child)
    l_store_i32_at(parent, OFF_CHILD_COUNT, count + 1)
}

pub fun child_at(ptr n, i32 index) -> ptr {
    return l_load_ptr_at(l_load_ptr_at(n, OFF_CHILD_DATA), index * 8)
}

// --- line tags -------------------------------------------------------------

fun grow_lines(ptr n, i32 need) -> void {
    i32 cap = l_load_i32_at(n, OFF_LINE_CAP)
    if need <= cap {
        return
    }
    if cap == 0 {
        cap = 16
    }
    while cap < need {
        cap = cap * 2
    }
    ptr fresh = l_alloc(cap * sizeof(i32))
    if l_ptr_is_null(fresh) != 0 {
        return
    }
    ptr old = l_load_ptr_at(n, OFF_LINE_DATA)
    i32 count = l_load_i32_at(n, OFF_LINE_COUNT)
    i32 i = 0
    while i < count {
        l_store_i32_at(fresh, i * 4, l_load_i32_at(old, i * 4))
        i = i + 1
    }
    if l_ptr_is_null(old) == 0 {
        l_free(old)
    }
    l_store_ptr_at(n, OFF_LINE_DATA, fresh)
    l_store_i32_at(n, OFF_LINE_CAP, cap)
}

pub fun add_line(ptr n, i32 tag) -> void {
    i32 count = l_load_i32_at(n, OFF_LINE_COUNT)
    grow_lines(n, count + 1)
    ptr data = l_load_ptr_at(n, OFF_LINE_DATA)
    if l_ptr_is_null(data) != 0 {
        return
    }
    l_store_i32_at(data, count * 4, tag)
    l_store_i32_at(n, OFF_LINE_COUNT, count + 1)
}

pub fun line_tag_at(ptr n, i32 index) -> i32 {
    return l_load_i32_at(l_load_ptr_at(n, OFF_LINE_DATA), index * 4)
}

// --- concatenation ---------------------------------------------------------

// Replays the opened file exactly.
//
// `build_into` records on the root whether the source ended with '\n'. Every
// stored line was followed by one in the source, so emitting line-then-'\n'
// rebuilds the text and adds exactly one extra '\n' when the source had no
// trailing newline; that byte is trimmed back off here.
pub fun join(ptr root) -> String {
    out! := String("")
    out! = emit_node(root, out)
    if node_ends_with_newline(root) != 0 {
        return out
    }
    return drop_last_byte(out)
}

// Removes the final byte of `text`. Split into its own function so the trim
// never aliases the concat chain that built `text`.
fun drop_last_byte(String text) -> String {
    i32 n = text.length()
    if n == 0 {
        return text
    }
    ptr p = l_str_substr(text.c_str(), 0, n - 1)
    return String(p)
}

// Prefix-order emission: a node's own lines, then each child's entry text and
// subtree. Mirrors `build_into`, which appends in exactly that order.
// Entry text always came from a line terminated by '\n', so it always gets one.
fun emit_node(ptr n, String out!) -> String {
    out = emit_lines(n, out)
    i32 kids = l_load_i32_at(n, OFF_CHILD_COUNT)
    i32 i = 0
    while i < kids {
        ptr c = child_at(n, i)
        out = out + string_tab_get(l_load_i32_at(c, OFF_ENTRY_TAG))
        out = out + newline()
        out = emit_node(c, out)
        i = i + 1
    }
    return out
}

fun emit_lines(ptr n, String out!) -> String {
    i32 count = l_load_i32_at(n, OFF_LINE_COUNT)
    i32 i = 0
    while i < count {
        out = out + string_tab_get(line_tag_at(n, i))
        out = out + newline()
        i = i + 1
    }
    return out
}

// --- ownership -------------------------------------------------------------

// Nodes come from `l_alloc`; auto-free only covers locals, so every level must
// be released explicitly. String-table entries are owned by the table.
pub fun free_tree(ptr n) -> void {
    i32 count = l_load_i32_at(n, OFF_CHILD_COUNT)
    i32 i = 0
    while i < count {
        free_tree(child_at(n, i))
        i = i + 1
    }
    ptr cd = l_load_ptr_at(n, OFF_CHILD_DATA)
    if l_ptr_is_null(cd) == 0 {
        l_free(cd)
    }
    ptr ld = l_load_ptr_at(n, OFF_LINE_DATA)
    if l_ptr_is_null(ld) == 0 {
        l_free(ld)
    }
    l_free(n)
}




