fun create_user(String name, i32 age, String role) {
    print(name)
    print(age)
    print(role)
}

fun main() -> i32 {
    // Positional + keyword mix; keywords out of declaration order.
    create_user("Alice", role: "admin", age: 20)
    return 0
}
