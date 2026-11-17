// SEditor entry point — Stage 2: tree outline over a file or a whole directory.
//
// Usage: seditor.exe <source-file | directory>
//
// A file opens as one tree; a directory is scanned recursively for `*.l` and
// every file becomes a top-level row. Outline rows fold on click, and the
// right pane renders the joined tree (the source it represents).

import "stdlib/io.l"
import "stdlib/string.l"
import "stdlib/array.l"
import "stdlib/env.l"
import "stdlib/fs.l"
import "stdlib/path.l"
import "apps/seditor/strtab.l"
import "apps/seditor/tree.l"
import "apps/seditor/parser.l"
import "apps/seditor/row.l"
import "apps/seditor/ui.l"

const WIN_W = 1200
const WIN_H = 780
const OUTLINE_W = 320

fun cmdline_path() -> String {
    args := get_args()
    if args.len() > 1 {
        return args[1]
    }
    return String("")
}

// Keeps a scroll offset inside [0, max]; negative maxima collapse to 0.
fun clamp_scroll(i32 v, i32 max) -> i32 {
    if v < 0 {
        return 0
    }
    if max > 0 && v > max {
        return max
    }
    return v
}

// Both panes are rebuilt from the tree, so a fold immediately changes the
// right-hand text length and the scroll clamps stay correct.
pub fun main() -> i32 {
    path := cmdline_path()
    if path.length() == 0 {
        print("usage: seditor <file|directory>\n")
        return 1
    }

    bool is_dir_mode = is_dir(path)
    root := build_path(path)
    if is_dir_mode {
        root = build_dir_tree(path)
    }

    w := open("SEditor - " + path, WIN_W, WIN_H)
    if l_ptr_is_null(w) != 0 {
        print("failed to create window\n")
        return 1
    }

    fnt := font("Consolas", 12, false)
    set_font(fnt)
    lh := line_height(fnt)

    outline_scroll! := 0
    source_scroll! := 0

    while pump() {
        while event_peek() {
            t := event_type()
            if t == EV_CLOSE {
                request_close()
            } else if t == EV_WHEEL {
                step := event_wheel() * lh * 3
                if event_mouse_x() < OUTLINE_W {
                    outline_scroll! = outline_scroll - step
                } else {
                    source_scroll! = source_scroll - step
                }
            } else if t == EV_MOUSE_DOWN && event_button() == 1 {
                if event_mouse_x() < OUTLINE_W && event_mouse_y() < client_height() {
                    rows := collect_rows(root)
                    idx := hit_row(rows, event_mouse_y(), outline_scroll, lh)
                    toggle_row(rows, idx)
                }
            } else if t == EV_KEY_DOWN {
                k := event_key()
                if k == VK_DOWN {
                    source_scroll! = source_scroll + lh
                } else if k == VK_UP {
                    source_scroll! = source_scroll - lh
                } else if k == VK_PRIOR {
                    source_scroll! = source_scroll - lh * 20
                } else if k == VK_NEXT {
                    source_scroll! = source_scroll + lh * 20
                } else if k == VK_ESCAPE {
                    request_close()
                }
            }
            event_pop()
        }

        rows := collect_rows(root)
        lines := split_view(join(root))

        cw := client_width()
        ch := client_height()

        outline_scroll! = clamp_scroll(outline_scroll, rows.len() * lh - ch)
        source_scroll! = clamp_scroll(source_scroll, lines.len() * lh - ch)

        hot := -1
        if mouse_x() < OUTLINE_W && mouse_y() < ch {
            hot = hit_row(rows, mouse_y(), outline_scroll, lh)
        }

        frame_begin()
        clear(28, 28, 30)
        draw_outline(rows, 0, 0, OUTLINE_W, ch, outline_scroll, fnt, hot)
        vline(OUTLINE_W, 0, ch, 60, 60, 64)
        draw_source(lines, OUTLINE_W + 1, 0, cw - OUTLINE_W - 1, ch, source_scroll, fnt)
        frame_end()
    }

    // No `free_tree(root)` on exit: the process is ending and the string table
    // still references interned lines, so freeing here only risks a double free.
    return 0
}
