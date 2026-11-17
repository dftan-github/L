// SEditor round-trip check.
//
// Builds the tree for a file, joins it back, and asserts byte equality with
// the original. This is the property the whole editor design rests on, so it
// is a standalone entry point instead of living inside the GUI.
//
// A directory argument is walked recursively and its `*.l` files are checked
// the same way, which exercises the exact load path `seditor.exe` uses.
//
// NOTE: the comparison deliberately returns a plain bool and leaves all
// printing to `report`. Folding the compare + print into one function that also
// calls `join` miscompiles (measured: a single CRLF file gained one byte) once
// the recursive walk is present in the same module. Keeping the decision and
// the rendering apart avoids that.
//
// Usage: seditor_check.exe <file|directory> [more...]

import "stdlib/io.l"
import "stdlib/string.l"
import "stdlib/array.l"
import "stdlib/env.l"
import "stdlib/fs.l"
import "stdlib/path.l"
import "apps/seditor/strtab.l"
import "apps/seditor/tree.l"
import "apps/seditor/parser.l"
import "apps/seditor/ui.l"

// True when `path` survives build -> join byte for byte.
fun check_one(String path) -> bool {
    content := read_all(path)
    if !content.is_some() {
        return false
    }
    original := content.unwrap()
    // `build_path` is what the GUI uses; keep exercising it here so a dangling
    // string-table entry would fail the check rather than ship silently.
    root := build_path(path)
    rebuilt := join(root)
    // `free_tree` is deliberately NOT called: the string table keeps every line
    // alive for the process lifetime, so freeing only adds a double-free risk.
    return l_str_eq(original.c_str(), rebuilt.c_str()) != 0
}

// Prints the outcome, with the first divergence when a file failed.
fun report(String path, bool ok) -> void {
    if ok {
        print("[OK] round-trip ")
        print(path)
        print("\n")
        return
    }
    content := read_all(path)
    if !content.is_some() {
        print("[ERROR] cannot read ")
        print(path)
        print("\n")
        return
    }
    original := content.unwrap()
    rebuilt := join(build_path(path))
    n := original.length()
    div! := 0
    i! := 0
    while i < n {
        if original.char_at(i) != rebuilt.char_at(i) {
            div! = i
            i! = n
        } else {
            i! = i + 1
        }
    }
    print("[FAIL] round-trip ")
    print(path)
    print(" diverges at byte ")
    print(from(div))
    print(" (orig ")
    print(from(n))
    print(", rebuilt ")
    print(from(rebuilt.length()))
    print(")\n")
}

// Recursive sweep. Returns the failure count of the files found under `dir`.
fun check_dir(String dir, i32 depth) -> i32 {
    if depth > 16 {
        return 0
    }
    res := read_dir(dir)
    if !res.is_ok() {
        print("[ERROR] cannot list ")
        print(dir)
        print("\n")
        return 1
    }
    entries := res.unwrap()
    failed! := 0
    i! := 0
    while i < entries.len() {
        name := path_join(dir, entries.get(i))
        if is_dir(name) {
            failed! = failed + check_dir(name, depth + 1)
        } else if path_ext(name) == ".l" {
            ok := check_one(name)
            report(name, ok)
            if !ok {
                failed! = failed + 1
            }
        }
        i! = i + 1
    }
    return failed
}

pub fun main() -> i32 {
    args := get_args()
    if args.len() < 2 {
        print("usage: seditor_check <file|directory> [more...]\n")
        return 1
    }
    failed! := 0
    i! := 1
    while i < args.len() {
        String arg = args.get(i)
        if is_dir(arg) {
            failed! = failed + check_dir(arg, 0)
        } else {
            ok := check_one(arg)
            report(arg, ok)
            if !ok {
                failed! = failed + 1
            }
        }
        i! = i + 1
    }
    if failed > 0 {
        return 1
    }
    return 0
}
