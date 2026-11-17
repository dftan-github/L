// [lcmd-path] The handler receives a HIERARCHICAL context path.
//
// A directive inside a method body sees "User.generate" (class + method), and
// its returned source is injected as STATEMENTS into that method — so
// `say_hello()` here is a free function declared at module level, called from
// the injected statement, not a method added to User.

import "stdlib/memory.l"
import "stdlib/io.l"

pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if "User.generate" == path && "make_hello" == cmd {
        return "print(say_hello());"
    }
    return ""
}

pub fun say_hello() -> i32 {
    return 42
}

class User {
    i32 id
    pub fun generate() -> i32 {
        //LCMD_make_hello()
        return 0
    }
}

fun main() -> i32 {
    u := User()
    u.generate()
    return 0
}
