fun create_user(String name, i32 age, String role) {
    print(name)
    print(age)
    print(role)
}

fun main() -> i32 {
    // Positional after keyword must be a parse error.
    create_user("Bob", age: 20, "user")
    return 0
}
