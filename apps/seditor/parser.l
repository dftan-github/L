// SEditor: turn a source file into an ordered Node tree.
//
// Column-0 declarations open a category; every other line becomes the owning
// category's line tag. Because categories and their lines are appended in file
// order, `join()` reproduces the file exactly.

import "stdlib/memory.l"
import "stdlib/io.l"
import "stdlib/string.l"
import "stdlib/array.l"
import "stdlib/file.l"
import "stdlib/path.l"
import "apps/seditor/strtab.l"
import "apps/seditor/tree.l"

const SPACE = 32
const TAB = 9
const INDENT_STEP = 4

// Leading whitespace as a column count (tab = INDENT_STEP).
fun indent_of(String s) -> i32 {
    col! := 0
    i! := 0
    while i < s.length() {
        c := s.char_at(i)
        if c == SPACE {
            col! = col + 1
        } else if c == TAB {
            col! = col + INDENT_STEP
        } else {
            return col
        }
        i! = i + 1
    }
    return col
}

fun classify(String text) -> i32 {
    t := text.trim()
    if t.length() == 0 {
        return KIND_LINE
    }
    if t.starts_with("//LCMD_") {
        return KIND_DIRECTIVE
    }
    if t.starts_with("//") {
        return KIND_COMMENT
    }
    if t.starts_with("class ") || t.starts_with("pub class ") ||
       t.starts_with("partial class ") || t.starts_with("interface ") ||
       t.starts_with("pub interface ") || t.starts_with("delegate ") {
        return KIND_CLASS
    }
    if t.starts_with("fun ") || t.starts_with("pub fun ") ||
       t.starts_with("static fun ") || t.starts_with("pub static fun ") {
        return KIND_FUNC
    }
    return KIND_LINE
}

// Builds the tree for a file's text. Only column-0 declarations open a node;
// indented lines (including indented comments) belong to the open category.
// Returns the raw root handle (see tree.l on why nodes are `ptr`).
pub fun build_text(String src) -> ptr {
    root := make_node(KIND_FILE, 1, string_tab_intern(src))
    build_into(root, src)
    return root
}

pub fun build_into(ptr root_ptr, String src) -> void {
    // `String.split(10)` keeps empty runs, but it does NOT necessarily end with
    // an empty part for a source ending in '\n' — that depends on how many
    // parts the array happened to hold. The trailing-newline fact is therefore
    // recorded on the root explicitly, and `join` reads it back instead of
    // guessing from the stored lines (a guess that cost several wrong fixes).
    i32 n = src.length()
    i32 ends_nl = 0
    if n > 0 && src.char_at(n - 1) == 10 {
        ends_nl = 1
    }
    set_ends_with_newline(root_ptr, ends_nl)
    lines := src.split(10)
    i32 end = lines.len()
    // Only an empty final part represents the source's trailing newline; it is
    // dropped here because `join` re-emits one '\n' per line.
    if end > 0 {
        String tail = lines.get(end - 1)
        if tail.length() == 0 {
            end = end - 1
        }
    }
    // The open category is tracked by its raw handle rather than a `Node`
    // cursor: a class value would be a copy and absorb the `add_line` writes.
    ptr open_node = l_null_ptr()
    i32 i = 0
    while i < end {
        String text = lines.get(i)
        i32 tag = string_tab_intern(text)
        i32 k = classify(text)
        bool inline = k == KIND_LINE || k == KIND_COMMENT || k == KIND_DIRECTIVE ||
                      indent_of(text) != 0
        if inline {
            if l_ptr_is_null(open_node) != 0 {
                add_line(root_ptr, tag)
            } else {
                add_line(open_node, tag)
            }
        } else {
            // Column-0 declaration: open a new category at this point. The new
            // node is kept as a raw handle so later lines land in it directly.
            ptr cat = make_node(k, i + 1, tag)
            push_child(root_ptr, cat)
            open_node = cat
        }
        i = i + 1
    }
}

// Reads a file for tree building. Missing files yield "" so the UI always has
// something to draw; `build_dir_tree` relies on this to skip unreadable files.
pub fun read_text(String path) -> String {
    content := read_all(path)
    if !content.is_some() {
        return String("")
    }
    return content.unwrap()
}

// Reads `path` and builds its tree; an unreadable file yields an empty tree so
// the UI always has something to render.
// Reads and builds in one call. The source is cloned first: `string_tab_intern`
// stores a reference (it never copies), so interning slices of the local
// `content` buffer would dangle once this frame returns and `content` is
// auto-freed. Callers that already own a live string should use `build_text`.
pub fun build_path(String path) -> ptr {
    src := read_text(path)
    // The root carries the file name as its label, never the source text:
    // `emit_node` prefixes every child with its entry text, so a source-carrying
    // root would duplicate the whole file in `join()`, and the outline row would
    // be one enormous line. The source itself is stored line by line below.
    root := make_node(KIND_FILE, 1, string_tab_intern(path_basename(path)))
    build_into(root, src)
    return root
}
