// Error case: the pattern's class does not match the scrutinee type.
class User {
    pub String name
}

class Admin {
    pub String name
}

fun main() -> i32 {
    a := Admin("Root")
    match a {
        User { name } => { print(name) }
        _ => { print("other") }
    }
    return 0
}
