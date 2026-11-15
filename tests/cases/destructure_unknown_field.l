// Error case: the destructured field does not exist on the class.
class User {
    pub String name
    pub i32 age
}

fun main() -> i32 {
    u := User("Ada", 36)
    match u {
        User { name, email } => { print(name) }
        _ => { print("other") }
    }
    return 0
}
