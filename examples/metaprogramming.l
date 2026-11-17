// Metaprogramming — every compile-time facility L has, in ONE runnable file.
//
// Two halves, both live here:
//   PART A — LCMD directives  : //LCMD_xxx(...) injects source at parse time.
//   PART B — comptime          : //LCMD-free, pure frontend blocks stripped
//                                before codegen.
//
// MARKERS — this file demonstrates the convention.
//
// An anchor names a piece of CONTENT, not a position. A marker is a comment
// whose sole leading token is a bracketed id, one space after the slashes:
//
//     `// [link:121-uid-counter] uid_counter() hands out a compile-time-unique id`
//
// A link is the same id in square brackets, either in this file's index below
// or written beside a path to another file:
//
//     `[link:121-uid-counter]`      `tests/cases/uid_test.l  [link:134-uid-monotonic]`
//
// Because an anchor travels with the code it describes, inserting or deleting
// lines elsewhere never invalidates a link. Only renaming the id does, and
// check_anchors.rb reports that immediately.

// Jump targets — every bracketed id below is defined by a `// [id]` marker
// further down in this file or in the linked file.
//
// PART A — LCMD directives (handler first, then its call sites):
//   A1  [link:115-directive-with-args]   directive with args + 5-arg handler (path, cmd, args, line, col)
//   A2  [link:117-class-body-site]    class-body injection -> methods / fields / delegate, path = "Point"
//   A3  [link:118-injected-statements]     statement injection -> statements into a function body
//   A4  [link:119-hierarchical-path]     hierarchical path -> "Point.generate" vs bare "Point"
//   A5  [link:121-uid-counter]   uid_counter() in a directive -> collision-free generated names
//   A6  [link:122-class-hook-on]    explicit class hook -> enable_class_hook + __on_class_parsed
//   A7  [link:124-source-line-column]    source_line/column() -> directive position inside the handler
//
// PART B — comptime:
//   B1  [link:125-comptime-block]      comptime { } -> frontend-only, stripped before codegen
//   B2  [link:126-comptime-assert] comptime_assert -> build veto on a false condition
//   B3  [link:127-comptime-uid]   uid_counter() -> monotonic, inlined as a constant
//   B4  [link:128-magic-file]    __FILE__ / __FUNC__ -> folded to string literals at codegen
//   B5  [link:130-magic-line]    __LINE__ -> folded to an i32 constant
//   B6  [link:131-zero-runtime-cost]     zero-cost proof -> 100k iterations, no comptime residue
//
// Same constants, evaluated inside comptime (a second marker for B4):
//   B4c [link:129-magic-file-comptime]   __FILE__ resolves while compiling
//
// Call sites (where the directives are literally written):
//   A2  [link:116-class-body-vs-method]         the three class-body directives
//   A4  [link:120-method-body-site]   the in-method directive
//   A6  [link:123-hook-opt-in]   hook enabled for Audited
//
// Isolation cases (same concept, one file each):
//   tests/cases/lcmd_in_class.l                [link:133-lcmd-in-class]  class-body members + in-method statements
//   tests/cases/lcmd_path.l                    [link:132-lcmd-context-path]      hierarchical path in a method body
//   tests/cases/uid_test.l                     [link:134-uid-monotonic]       uid_counter across two directives
//   tests/cases/context_macro_test.l           [link:135-line-preserved]  __FILE__ / __LINE__ / __FUNC__ at runtime
//   tests/cases/comptime_assert_fail_test.l    [link:136-comptime-veto]  comptime_assert vetoing a build
//
// Interpreter limits (compiler/comptime/interpreter.cpp): comptime accepts only
// int/bool/string literals, identifiers, calls and binary ops — no loops, no
// arrays, no user classes. source_line()/source_column() exist ONLY during
// compilation: emitting them into generated runtime code is a compile error,
// so A7 never leaks them into the injected L source.

import "stdlib/memory.l"
import "stdlib/io.l"
import "stdlib/array.l"
import "stdlib/string.l"

// ===========================================================================
// PART A — LCMD: directives are L functions returning L source.
// ===========================================================================

// [anchor:115-directive-with-args] [link:118-injected-statements] [link:121-uid-counter] [link:124-source-line-column]
// The 5-argument form receives the directive's exact source position, in addition
// to (path, cmd, args). `uid_counter()` is the only safe way to mint symbol names:
// it is monotonic across the whole compilation, so two directives in two files can
// never produce the same identifier.
pub fun __compile_time_handler(String path, String cmd, String args, i32 line, i32 col) -> String {
    // [anchor:117-class-body-site]
    // Written in the CLASS body, so `path` is the bare class name and the returned
    // source must parse as class members: methods, fields, `delegate`.
    if "Point" == path && "method" == cmd {
        return "pub fun injected() -> i32 { return 7; }"
    }
    if "Point" == path && "field" == cmd {
        return "pub i32 extra;"
    }
    if "Point" == path && "delegate" == cmd {
        return "delegate Bag;"
    }

    // [anchor:119-hierarchical-path]
    // The same class, but the directive sits in a METHOD body. The path gains the
    // method name, so "Point.generate" != "Point".
    if "Point.generate" == path && "in_method" == cmd {
        return "return 11;"
    }

    // [anchor:118-injected-statements]
    // `args` is the raw text inside the parentheses; `line`/`col` are the directive's
    // own source position, so generated code can report it.
    if "echo_args" == cmd && "true" == args {
        return "i32 echo_line = " + line + ";"
    }

    // [anchor:121-uid-counter]
    // A directive that generates uniquely-named locals. `uid_counter()` hands out a
    // compile-time-unique id, so repeated directives — or the same directive in two
    // files — can never emit the same identifier. The ids are also asserted monotonic
    // right here, during the build.
    if "gen_local" == cmd {
        first := uid_counter()
        second := uid_counter()
        comptime_assert(second == first + 1, "uid_counter must be monotonic in LCMD")
        return "i32 gen_" + args + " = " + first + ";"
    }

    // [anchor:124-source-line-column]
    // source_line()/source_column() evaluated DURING compilation. The numbers are baked
    // into the emitted source; the functions themselves are never called at runtime.
    if "where_directive" == cmd {
        return "i32 directive_line = " + source_line() + ";"
    }
    return ""
}

// [anchor:122-class-hook-on]
// Class hooks are OPT-IN. `__on_class_parsed` fires only for classes parsed while a
// `//LCMD_enable_class_hook()` is active; everything else takes the fast path and
// never touches the interpreter.
pub fun __on_class_parsed(String className) -> String {
    if "Audited" == className {
        return "pub fun audit_tag() -> i32 { return 999; }"
    }
    return ""
}

// An embeddable behaviour, used by A2 to prove `delegate` injection works.
pub class Bag {
    pub i32 stored
    pub fun get_stored() -> i32 { return stored; }
    pub fun set_stored(i32 v) { stored = v; }
}

// [anchor:116-class-body-vs-method]
// Members are injected straight into this class body — no `generate()` wrapper
// method required.
class Point {
    i32 id
    //LCMD_method()
    //LCMD_field()
    //LCMD_delegate()

    fun generate() -> i32 {
        // [anchor:120-method-body-site]
        // Path here is "Point.generate", not "Point".
        //LCMD_in_method()
        return 0;
    }

    pub fun own() -> i32 { return 5; }
}

// [anchor:123-hook-opt-in]
// The hook is enabled for exactly these two declarations, then disabled. The injected
// source is parsed at MODULE level, so the hook emits a free function — that is the
// documented contract, not a class member.
//LCMD_enable_class_hook()
class Audited {
    i32 x
}
//LCMD_disable_class_hook()

class NotAudited {
    i32 x
}

// ===========================================================================
// PART B — comptime: frontend-only, gone by codegen.
// ===========================================================================

// B1: frontend-only block, stripped before codegen.
// [anchor:125-comptime-block]
//
// B2: both assertions hold, so the build proceeds. Flip either and the compiler
// stops with `[ERROR] file:line:col - Compile-time error: ...`.
// [anchor:126-comptime-assert]
comptime {
    comptime_assert(1 + 1 == 2, "arithmetic must hold at compile time")
    comptime_assert("a" + "b" == "ab", "string concat must hold at compile time")
}

// [anchor:127-comptime-uid]
// uid_counter() returns the current counter and increments it. The values are inlined as
// constants, so `id_second == id_first + 1` and no runtime call exists. This is the same
// counter PART A's directives consume.
comptime {
    i32 id_first = uid_counter()
    i32 id_second = uid_counter()
    comptime_assert(id_second == id_first + 1, "uid_counter must be monotonic")
}

// B4 + B5 inside comptime: the magic constants resolve here too. __FUNC__ has no
// function here, so it reports "<comptime>", and __LINE__ is the block's line.
// [anchor:129-magic-file-comptime]
comptime {
    comptime_assert(__FILE__ != "", "__FILE__ must resolve inside comptime")
    if 42 >= 42 {
        print("[comptime] B1: arithmetic checked while compiling")
    }
}

// B4 + B5 at RUNTIME: intercepted at codegen, folded to literals.
// [anchor:128-magic-file]
// [anchor:130-magic-line]
pub fun where_am_i() -> i32 {
    print(__FILE__)
    print(__FUNC__)
    print(__LINE__)
    return 0
}

// [anchor:131-zero-runtime-cost]
// 100k iterations. If any comptime machinery survived into the binary this would still be
// correct — the assertion is on the IR, in run_tests.rb.
pub fun hot_loop() -> i32 {
    total! := 0
    i! := 0
    while i < 100000 {
        total! = total + 3
        i! = i + 1
    }
    return total
}

fun main() -> i32 {
    // ---- A2/A4: class members injected without any wrapper method ----
    p := Point()
    p.extra = 4
    p.set_stored(3)
    print(p.injected())
    print(p.own())
    print(p.get_stored())
    print(p.extra)
    print(p.generate())

    // ---- A1: args + 5-arg handler position ----
    //LCMD_echo_args(true)
    print(echo_line)

    // ---- A5: unique names from uid_counter() ----
    //LCMD_gen_local(answer)
    print(gen_answer)

    // ---- A7: directive position, baked in at compile time ----
    //LCMD_where_directive()
    print(directive_line)

    // ---- A6: hook fired for Audited only (module-level injection) ----
    print(audit_tag())

    // ---- B4/B5: runtime-folded magic constants ----
    where_am_i()

    // ---- B5: __LINE__ folded into an i32 constant ----
    print(__LINE__)

    // ---- B6: the loop carries zero comptime residue ----
    print(hot_loop())
    return 0
}
