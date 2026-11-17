// SEditor: minimal GDI outline + source view.
//
// Vertical layout only; both panes scroll by pixel offset so half-lines never
// appear. Everything is immediate-mode drawing onto the runtime back buffer.

import "stdlib/win32.l"
import "stdlib/string.l"
import "stdlib/array.l"
import "stdlib/memory.l"
import "stdlib/path.l"
import "stdlib/fs.l"
import "apps/seditor/tree.l"
import "apps/seditor/row.l"
import "apps/seditor/strtab.l"

const INDENT_W = 12

// A source line as a string-table tag. The text itself is read back through
// `string_tab_get`, which owns a stable copy — no second field needed.
pub class ViewLine {
    i32 tag
}

// Splits on '\n' into ViewLines. A trailing newline yields no empty final
// entry, matching how `join` reassembles the file.
pub fun split_view(String src) -> Array<ViewLine> {
    out := Array<ViewLine>()
    n := src.length()
    start! := 0
    i! := 0
    while i < n {
        if src.char_at(i) == 10 {
            piece := src.slice(start, i - start)
            vl := heap ViewLine()
            vl.tag = string_tab_intern(piece)
            // `intern` copies, so read the text back from the table instead of
            // keeping `piece` alive (its buffer is a temporary).
            out.push(vl)
            start! = i + 1
        }
        i! = i + 1
    }
    if start < n {
        piece := src.slice(start, n - start)
        vl := heap ViewLine()
        vl.tag = string_tab_intern(piece)
        out.push(vl)
    }
    return out
}

// Flattens the tree into outline rows (depth-first, children in order).
// Collapsed nodes contribute their own row but none of their descendants.
pub fun push_rows(ptr n, i32 depth, Array<Row> out) -> void {
    String label = string_tab_get(node_entry_tag(n)).trim()
    i32 kids = node_child_count(n)
    out.push(make_row(depth, label, node_line_no(n), n, node_collapsed(n), kids))
    if node_collapsed(n) != 0 {
        return
    }
    i! := 0
    while i < kids {
        push_rows(child_at(n, i), depth + 1, out)
        i! = i + 1
    }
}

pub fun collect_rows(ptr root) -> Array<Row> {
    rows := Array<Row>()
    push_rows(root, 0, rows)
    return rows
}

// Appends every `*.l` file found under `dir` (recursive), as a tree:
//   dir -> subdir/file.l
// Unreadable or oversized files are skipped rather than aborting the walk.
const MAX_FILE_BYTES = 262144

fun walk(String dir, i32 depth, Array<String> out) -> void {
    if depth > 16 {
        return
    }
    res := read_dir(dir)
    if !res.is_ok() {
        return
    }
    entries := res.unwrap()
    i! := 0
    while i < entries.len() {
        name := path_join(dir, entries.get(i))
        if is_dir(name) {
            walk(name, depth + 1, out)
        } else if path_ext(name) == ".l" {
            out.push(name)
        }
        i! = i + 1
    }
}

pub fun scan_dir(String dir) -> Array<String> {
    out := Array<String>()
    walk(dir, 0, out)
    return out
}

// Builds one category node per file under `dir`, recursively shown by file
// basename. Returns a KIND_DIRECTORY root node.
pub fun build_dir_tree(String dir) -> ptr {
    root := make_node(KIND_DIRECTORY, 1, string_tab_intern(dir))
    files := scan_dir(dir)
    i! := 0
    while i < files.len() {
        path := files.get(i)
        ptr cat = make_node(KIND_FILE, 1, string_tab_intern(path_basename(path)))
        build_into(cat, read_text(path))
        push_child(root, cat)
        i! = i + 1
    }
    return root
}

// Outline pane scrolled by `scroll` pixels. Guides are drawn per depth level so
// nesting reads at a glance; `hot` is the row under the mouse (-1 if none).
pub fun draw_outline(Array<Row> rows, i32 x, i32 y, i32 w, i32 h, i32 scroll, i32 font, i32 hot) -> void {
    lh := line_height(font)
    push_clip(x, y, w, h)
    i! := 0
    while i < rows.len() {
        ry := y + i * lh - scroll
        if ry + lh > y && ry < y + h {
            row := rows.get(i)
            if i == hot {
                rect(x, ry, w, lh, 44, 44, 50)
            }
            tx := x + 8 + row.depth * INDENT_W
            lvl! := 0
            while lvl < row.depth {
                vline(x + 8 + lvl * INDENT_W + 4, ry, lh, 52, 52, 58)
                lvl! = lvl + 1
            }
            if row.has_kids > 0 {
                mark := "\u25be"
                if row.collapsed != 0 {
                    mark = "\u25b8"
                }
                text(tx, ry, mark, 130, 150, 170)
            }
            tint := 190
            if row.line_no == 1 {
                tint = 255
            }
            text(tx + 10, ry, row.label, tint, tint, tint)
        }
        i! = i + 1
    }
    pop_clip()
}

// Outline row index under screen y, or -1 when the point misses every row.
pub fun hit_row(Array<Row> rows, i32 y_pane, i32 scroll, i32 lh) -> i32 {
    if lh <= 0 {
        return -1
    }
    i := (y_pane + scroll) / lh
    if i < 0 || i >= rows.len() {
        return -1
    }
    return i
}

// Toggles the fold state of the row under the cursor. Collapsing also clears
// descendant flags so re-expanding shows the full subtree.
pub fun toggle_row(Array<Row> rows, i32 index) -> void {
    if index < 0 || index >= rows.len() {
        return
    }
    row := rows.get(index)
    if row.has_kids == 0 {
        return
    }
    if row.collapsed != 0 {
        set_collapsed(row.handle, 0)
        collapse_descendants(row.handle, 0)
    } else {
        set_collapsed(row.handle, 1)
    }
}

// Source pane: numbered lines, scrolled by `scroll` pixels.
pub fun draw_source(Array<ViewLine> lines, i32 x, i32 y, i32 w, i32 h, i32 scroll, i32 font) -> void {
    lh := line_height(font)
    push_clip(x, y, w, h)
    i! := 0
    while i < lines.len() {
        ly := y + i * lh - scroll
        if ly + lh > y && ly < y + h {
            text(x + 8, ly, from(i + 1), 110, 110, 110)
            text(x + 56, ly, string_tab_get(lines.get(i).tag), 220, 220, 220)
        }
        i! = i + 1
    }
    pop_clip()
}
