// [lcmd-in-place] `//LCMD_xxx()` written directly in a class body.
//
// The path seen by the handler is the bare class name, and whatever the handler
// returns is parsed as CLASS MEMBERS (methods, fields, delegates) — no
// `generate()` wrapper method needed.
//
// The legacy in-method form keeps the `Class.method` path, but a directive in a
// METHOD BODY injects STATEMENTS into that method — not class members.

import "stdlib/io.l"

pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if "User" == path && "method" == cmd {
        return "pub fun injected() -> i32 { return 7; }"
    }
    if "User" == path && "field" == cmd {
        return "pub i32 extra;"
    }
    if "User" == path && "delegate" == cmd {
        return "delegate Bag;"
    }
    // Legacy form: a directive inside a METHOD body injects STATEMENTS into
    // that method (the path carries the method name). Injecting a `pub fun`
    // here would be a nested function, which L does not have.
    if "User.generate" == path && "legacy" == cmd {
        return "print(42);"
    }
    return ""
}

pub class Bag {
    pub i32 stored
    pub fun get_stored() -> i32 { return stored; }
    pub fun set_stored(i32 v) { stored = v; }
}

class User {
    i32 id
    //LCMD_method()
    //LCMD_field()
    //LCMD_delegate()
    fun generate() -> i32 {
        //LCMD_legacy()
        return 0;
    }
    pub fun own() -> i32 { return 5; }
}

fun main() -> i32 {
    u := User()
    u.extra = 4
    u.set_stored(3)
    // Injected methods, fields and delegate-promoted methods all resolve.
    // Returned as the sum so the check needs no string building (one `print`
    // per value; the runner compares the concatenated lines).
    print(u.injected())
    print(u.own())
    print(u.get_stored())
    print(u.extra)
    // The in-method directive injected `print(42);` into generate().
    u.generate()
    return 0
}
